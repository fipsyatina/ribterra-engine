"""Exercise actual encrypted UDP through two bounded, lossy loopback relays."""
import argparse
import heapq
import json
from pathlib import Path
import random
import selectors
import socket
import subprocess
import tempfile
import threading
import time


class Relay:
    def __init__(self, server, seed, loss):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.setblocking(False)
        self.server = server
        self.client = None
        self.random = random.Random(seed)
        self.loss = loss / 100
        self.queue = []
        self.sequence = 0
        self.stats = {name: 0 for name in (
            "received", "forwarded", "random_drops", "outage_drops", "duplicates",
            "reorder_delays", "bytes_forwarded", "peak_queued_datagrams")}
        self.directions = {"client_to_server": 0, "server_to_client": 0}

    @property
    def endpoint(self):
        return "127.0.0.1:" + str(self.socket.getsockname()[1])

    def receive(self, now, cut):
        # Limit per-iteration work even if a peer sends an unexpected flood.
        for _ in range(256):
            try:
                data, sender = self.socket.recvfrom(65535)
            except BlockingIOError:
                break
            self.stats["received"] += 1
            if sender == self.server:
                destination = self.client
                direction = "server_to_client"
            else:
                self.client = sender
                destination = self.server
                direction = "client_to_server"
            if destination is None:
                continue
            self.directions[direction] += 1
            if cut:
                self.stats["outage_drops"] += 1
                continue
            if self.random.random() < self.loss:
                self.stats["random_drops"] += 1
                continue
            # 75 ms in each direction: configured base RTT is 150 ms.
            delay = 0.075
            if self.random.random() < 0.05:
                delay += 0.09
                self.stats["reorder_delays"] += 1
            self.enqueue(now + delay, data, destination)
            if self.random.random() < 0.02:
                self.stats["duplicates"] += 1
                self.enqueue(now + delay + 0.01, data, destination)

    def enqueue(self, due, data, destination):
        if len(self.queue) >= 8192:
            raise RuntimeError("UDP relay queue exceeded its declared bound")
        self.sequence += 1
        heapq.heappush(self.queue, (due, self.sequence, data, destination))
        self.stats["peak_queued_datagrams"] = max(
            self.stats["peak_queued_datagrams"], len(self.queue))

    def forward(self, now, cut):
        while self.queue and self.queue[0][0] <= now:
            _, _, data, destination = heapq.heappop(self.queue)
            if cut:
                self.stats["outage_drops"] += 1
                continue
            self.socket.sendto(data, destination)
            self.stats["forwarded"] += 1
            self.stats["bytes_forwarded"] += len(data)


def run(executable, loss, root):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        server = reservation.getsockname()
    relays = [Relay(server, 70421 + loss * 100 + index, loss) for index in range(2)]
    stop = threading.Event()
    errors = []

    def pump():
        try:
            with selectors.DefaultSelector() as selector:
                for relay in relays:
                    selector.register(relay.socket, selectors.EVENT_READ, relay)
                while not stop.is_set():
                    cut = (root / "link.cut").exists()
                    acknowledged = root / "link.cut.active"
                    if cut and not acknowledged.exists():
                        acknowledged.write_text("active", encoding="ascii")
                    elif not cut and acknowledged.exists():
                        acknowledged.unlink()
                    now = time.monotonic()
                    for key, _ in selector.select(0.002):
                        relay = key.data
                        relay.receive(now, cut and relay is relays[1])
                    for relay in relays:
                        relay.forward(time.monotonic(), cut and relay is relays[1])
        except BaseException as error:
            errors.append(str(error))

    worker = threading.Thread(target=pump, name="ribterra-udp-relays", daemon=True)
    worker.start()
    try:
        result = subprocess.run(
            [str(executable), str(server[1]), relays[0].endpoint, relays[1].endpoint, str(root)],
            capture_output=True, text=True, encoding="utf-8", timeout=160)
    except subprocess.TimeoutExpired as error:
        # Preserve progress in the test log when the outer bound wins before
        # the native scenario can print its final JSON. The timeout is unchanged.
        for stream, output in (("stdout", error.stdout), ("stderr", error.stderr)):
            if output:
                if isinstance(output, bytes):
                    output = output.decode("utf-8", errors="replace")
                print(f"Timed-out scenario {stream}:\n{output}", flush=True)
        raise
    finally:
        stop.set()
        worker.join(timeout=3)
        for relay in relays:
            relay.socket.close()
    report = {
        "test": "manual-orders-over-impaired-encrypted-udp", "base_rtt_ms": 150,
        "loss_percent_per_direction": loss, "duplicate_percent": 2,
        "extra_delay_percent": 5, "extra_delay_ms": 90,
        "relays": [{**relay.stats, "directions": relay.directions} for relay in relays],
        "relay_errors": errors, "exit_code": result.returncode,
        "scenario": result.stdout.strip(), "stderr": result.stderr.strip(),
        "limits": "Two clients on one host, deterministic impairment decisions; not physical-PC or throughput acceptance.",
    }
    (root / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))
    if errors or result.returncode:
        raise RuntimeError("Adverse network scenario failed; see result.json")
    for relay in relays:
        assert all(value > 0 for value in relay.directions.values()), relay.stats
        assert relay.stats["random_drops"] and relay.stats["reorder_delays"], relay.stats
        assert relay.stats["bytes_forwarded"] > 10000, relay.stats
    assert relays[1].stats["outage_drops"] > 0, relays[1].stats


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--loss", type=int, choices=[1, 3], required=True)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    if arguments.output:
        arguments.output.mkdir(parents=True, exist_ok=False)
        run(arguments.executable.resolve(), arguments.loss, arguments.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="ribterra-adverse-") as directory:
            run(arguments.executable.resolve(), arguments.loss, Path(directory))


if __name__ == "__main__":
    main()
