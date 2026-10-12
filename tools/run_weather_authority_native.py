"""Run the frozen CPU authority executable once; retain actual process evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time

REPO = Path(r"C:\Users\fipst\Desktop\ribterra")


def row(path: Path) -> dict:
    if not path.is_file() or path.is_symlink():
        raise RuntimeError(f"Missing regular bound file: {path}")
    return {"path": str(path), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def bindings(value, result: dict[str, dict]) -> None:
    if isinstance(value, dict):
        if {"path", "bytes", "sha256"} <= value.keys():
            actual = row(Path(value["path"]))
            if actual["bytes"] != value["bytes"] or actual["sha256"] != value["sha256"].lower():
                raise RuntimeError(f"Build input changed: {value['path']}")
            result[actual["path"]] = actual
        for child in value.values():
            bindings(child, result)
    elif isinstance(value, list):
        for child in value:
            bindings(child, result)


def write_new(path: Path, value: dict) -> None:
    with path.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(value, output, ensure_ascii=False, indent=2)
        output.write("\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    output = args.output.resolve()
    base = REPO / "build"
    if build.parent != base or output.parent != base or output.exists() or build == output:
        raise RuntimeError("Build/output must be distinct immediate repository build children; output fresh")
    if build.is_symlink() or build.is_junction() or shutil.disk_usage(base).free < 384 * 1024**2:
        raise RuntimeError("Invalid build ancestry or insufficient free space")
    report_path = build / "weather-authority-build-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8-sig"))
    if report["root"] != str(build) or report["complete"] is not True:
        raise RuntimeError("Incomplete or mismatched domain build")
    inputs = {}
    bindings(report, inputs)
    inputs[str(report_path)] = row(report_path)
    runner = Path(__file__).resolve(strict=True)
    inputs[str(runner)] = row(runner)
    executable = build / "WeatherAuthorityProbe.exe"
    if report["executable"]["path"] != str(executable):
        raise RuntimeError("Executable path differs from frozen build")
    argv = [str(executable), str(output)]
    start = time.perf_counter()
    process = subprocess.run(argv, cwd=build, capture_output=True, text=True, encoding="utf-8", errors="replace")
    elapsed_ms = (time.perf_counter() - start) * 1000
    if not output.exists():
        output.mkdir()
    native_files = []
    for path in sorted(output.rglob("*")):
        if path.is_symlink() or path.is_junction():
            raise RuntimeError(f"Reparse output: {path}")
        if path.is_file():
            native_files.append({**row(path), "relative_path": path.relative_to(output).as_posix()})
    errors = []
    after = []
    for before in inputs.values():
        current = row(Path(before["path"]))
        unchanged = current == before
        after.append({"before": before, "after": current, "unchanged": unchanged})
        if not unchanged:
            errors.append(f"Input changed during native execution: {before['path']}")
    native_report = output / "weather-authority-report.json"
    native_complete = False
    if native_report.is_file():
        try:
            native_complete = json.loads(native_report.read_text(encoding="utf-8"))["complete"] is True
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(f"Native report could not be read: {type(error).__name__}: {error}")
    else:
        errors.append("Native domain report is missing")
    if sum(path.stat().st_size for path in output.rglob("*") if path.is_file()) > 128 * 1024**2:
        errors.append("Native root exceeds128MiB")
    accepted = process.returncode == 0 and native_complete and not errors and "weather authority checks passed" in process.stdout
    transcript = output / "execution-output.txt"
    with transcript.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(process.stdout)
        stream.write(process.stderr)
    write_new(output / "native-execution.json", {"schema": 1, "actual_subprocess": True,
        "actual_argv": argv, "build_root": str(build), "output_root": str(output),
        "exit_code": process.returncode, "elapsed_ms": elapsed_ms, "accepted": accepted,
        "inputs": after, "bound_inputs_unchanged": not errors, "integrity_errors": errors,
        "native_output_before_runner_evidence": native_files, "transcript": row(transcript),
        "scope": "World/save/wire/loopback CPU integration; Activity/production renderer/reference acceptance OPEN"})
    print(process.stdout, end="")
    print(process.stderr, end="")
    if not accepted:
        raise SystemExit(process.returncode or 1)


if __name__ == "__main__":
    main()
