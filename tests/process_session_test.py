"""Real dedicated server plus two independent GNS client processes."""
import json
import os
from pathlib import Path
import subprocess
import socket
import sys
import tempfile
import time


def main():
    server_exe, client_exe = map(str, map(Path, sys.argv[1:3]))
    with tempfile.TemporaryDirectory(prefix="ribterra-поселення-") as directory:
        root = Path(directory)
        ready = root / "ready.json"
        save = root / "settlement.save"
        environment = os.environ.copy()
        # Test credential is confined to these child processes, never printed.
        environment["RIBTERRA_SERVER_PASSWORD"] = "тест-¿Ribterra?-" + os.urandom(24).hex()
        # GNS requires an explicit port. The server still detects a bind race.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        with (root / "server.log").open("w", encoding="utf-8") as log:
            server = subprocess.Popen(
                [server_exe, "--port", str(port), "--world", str(save), "--ticks", "300",
                 "--ready-file", str(ready)],
                stdout=log, stderr=log, env=environment,
            )
            clients = []
            try:
                deadline = time.monotonic() + 10
                while not ready.exists() and time.monotonic() < deadline:
                    if server.poll() is not None:
                        raise RuntimeError("Dedicated server exited before readiness")
                    time.sleep(0.02)
                if not ready.exists():
                    raise RuntimeError("Dedicated server readiness timed out")
                address = "127.0.0.1:" + str(json.loads(ready.read_text(encoding="utf-8"))["port"])
                for name in ("Process A", "Process B"):
                    clients.append(subprocess.Popen(
                        [client_exe, "--endpoint", address, "--name", name,
                         "--build", "--chat", "Hello from " + name],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        text=True, encoding="utf-8", env=environment,
                    ))
                results = []
                for client in clients:
                    stdout, stderr = client.communicate(timeout=20)
                    if client.returncode:
                        raise RuntimeError("Client failed: " + stderr)
                    results.append(json.loads(stdout.strip()))
                assert results[0]["player"] != results[1]["player"], results
                assert all(x["residents"] == 10 and x["buildings"] == 2 for x in results), results
                assert results[0]["wood"] == results[1]["wood"], results
                assert sorted(x["command_reply_status"] for x in results) == ["applied", "rejected"], results
                assert all(x["wood_total_before_delivery"] == 80 and
                           x["wood_available_after_reservation"] == 50 for x in results), results
                assert all(x["wood"] == 50 and x["wood_available"] == 50 and
                           x["wood_reserved"] == 0 for x in results), results
                assert all(x["saw_reserved_materials"] and x["saw_carried_materials"] and
                           x["saw_delivery"] and x["construction_completed"] for x in results), results
                assert all(x["chat"] and x["command_reply_terminal"] for x in results), results
                if len(sys.argv) == 5:
                    screenshot = Path(sys.argv[4])
                    graphical_args = [sys.argv[3], "--connect", address, "--name", "Rendered client",
                                      "--compact", "--capture", str(screenshot)]
                    if environment.get("SDL_VIDEODRIVER", "").lower() == "dummy" or \
                            environment.get("RIBTERRA_TEST_SOFTWARE_RENDERER") == "1":
                        graphical_args.append("--software")
                    graphical = subprocess.run(
                        graphical_args,
                        capture_output=True, text=True, encoding="utf-8", env=environment, timeout=15,
                    )
                    if graphical.returncode:
                        raise RuntimeError("Graphical client failed: " + graphical.stderr)
                    assert screenshot.is_file() and screenshot.stat().st_size > 10000, "Missing remote framebuffer"
                if server.wait(timeout=20) != 0:
                    raise RuntimeError("Dedicated server stopped with error")
                assert save.is_file() and save.stat().st_size > 100, "Server did not save"
                print(json.dumps({"test": "dedicated-two-process-clients", "results": results}))
            finally:
                for process in [*clients, server]:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                if server.returncode:
                    log.flush()
                    print((root / "server.log").read_text(encoding="utf-8"), file=sys.stderr)


if __name__ == "__main__":
    main()
