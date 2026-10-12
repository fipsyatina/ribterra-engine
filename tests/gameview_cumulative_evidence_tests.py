"""Read-only current source/build/native/repeat checks for the bounded checkpoint."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hash_row(row: dict, path: Path) -> None:
    require(path.is_file(), f"Missing bound file: {path}")
    require(path.stat().st_size == row["bytes"] and digest(path) == row["sha256"].lower(),
            f"Bound file changed: {path}")


def bound_rows(value, checked: set[Path]) -> None:
    if isinstance(value, dict):
        if {"path", "sha256", "bytes"} <= value.keys():
            path = Path(value["path"])
            require(path.is_absolute(), f"Ambiguous bound path: {path}")
            hash_row(value, path)
            checked.add(path)
        for child in value.values():
            bound_rows(child, checked)
    elif isinstance(value, list):
        for child in value:
            bound_rows(child, checked)


def load(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def verify_current_cohort(build: Path, kit: Path, first: Path, second: Path,
                          required_sources: set[str], result_name: str,
                          save_exceptions: set[str]) -> tuple[list[dict], dict]:
    """Shared read-only bindings/repeat oracle for actual GameView probes."""
    build, kit, first, second = (p.resolve(strict=True) for p in (build, kit, first, second))
    require(first != second, "Two distinct native executions are required")
    report = load(build / "build-report.json")
    require(Path(report["output_root"]) == build and Path(report["control_kit"]["root"]) == kit,
            "Build/kit root binding mismatch")
    checked: set[Path] = set()
    bound_rows(report, checked)
    for row in report["runtime_files"]:
        for field in ("origin", "destination"):
            hash_row(row, Path(row[field]))
            checked.add(Path(row[field]))
    sources = report["source_provenance"]
    require(sources and len({row["relative_path"] for row in sources}) == len(sources),
            "Empty or duplicate source closure")
    require(required_sources <= {row["relative_path"] for row in sources}, "Incomplete cumulative source closure")
    for row in sources:
        for field in ("repository_path", "snapshot_path"):
            hash_row(row, Path(row[field]))
    final_sources = report["source_final_binding"]
    require(len(final_sources) == len(sources) and
            {row["relative_path"] for row in final_sources} == {row["relative_path"] for row in sources} and
            all(row["unchanged"] for row in final_sources),
            "Builder reported source mutation")
    require(report["protected_binding"] and all(row["unchanged"] for row in report["protected_binding"]),
            "Builder reported protected-file mutation")
    sidecars = sorted(build.glob("*.log.exit.txt"))
    command_paths = {Path(row["path"]) for row in report["command_evidence"]}
    expected_commands = set(sidecars) | {Path(str(p).removesuffix(".exit.txt")) for p in sidecars}
    require(command_paths == expected_commands, "Command log/exit inventory is incomplete")
    require(sidecars and all(path.read_text(encoding="utf-8-sig").strip() == "0" for path in sidecars),
            "Missing or failing command sidecars")
    second_kit = kit.parent / "export-b"
    kit_files = {p.relative_to(kit) for p in kit.rglob("*") if p.is_file()}
    require(second_kit.is_dir() and kit_files == {p.relative_to(second_kit) for p in second_kit.rglob("*") if p.is_file()},
            "Two retained kit export inventories differ")
    for relative in kit_files:
        require((kit / relative).read_bytes() == (second_kit / relative).read_bytes(),
                f"Kit exporter repeat differs: {relative}")
    native_inventories = []
    results = []
    for root in (first, second):
        execution = load(root / "native-execution.json")
        require(execution["accepted"] and execution["actual_subprocess"] and
                execution["exit_code"] == 0 and execution["bound_inputs_unchanged"] and
                not execution["integrity_errors"], "Native execution not accepted")
        require(Path(execution["build_root"]) == build and Path(execution["kit_root"]) == kit and
                Path(execution["output_root"]) == root, "Native cohort mismatch")
        require(execution["actual_argv"] == [str(build / "GameViewWaterProbe.exe"), str(build / "assets"),
                                            str(kit), str(root)], "Actual native arguments differ")
        bound_rows(execution, checked)
        recorded = execution["native_output_before_runner_evidence"]["files"]
        inventory = {row["relative_path"] for row in recorded}
        actual = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}
        require(len(inventory) == len(recorded) and
                actual == inventory | {"native-execution.json", "execution-output.txt"},
                "Native output ledger is incomplete")
        for row in recorded:
            require(Path(row["path"]) == root / row["relative_path"], "Native file path binding differs")
        native_inventories.append(inventory)
        native = load(root / result_name)
        require(native["complete"] and native["full_engine"] == "OPEN" and
                native["reference_fidelity"] == "NOT_ACCEPTED", "Incorrect checkpoint claim")
        require(native["fps"] is None and native["gpu_execution_ms"] is None,
                "Unmeasured performance claim")
        require(native["captures"] and all((root / row["file"]).is_file() for row in native["captures"]),
                "Missing recorded captures")
        for capture in native["captures"]:
            header = (root / capture["file"]).read_bytes()[:24]
            require(header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR" and
                    len(header) == 24 and list(struct.unpack(">II", header[16:24])) == native["resolution"],
                    "Native capture resolution differs from the report")
        results.append(native)
    require(native_inventories[0] == native_inventories[1], "Recursive native output inventories differ")
    # Fresh World identity is intentionally random. Each save is hashed against
    # its own execution ledger, while every derived fixture/result must repeat.
    require(save_exceptions <= native_inventories[0] and
            all(name.endswith(".save") for name in save_exceptions), "Invalid repeat save exceptions")
    outputs = native_inventories[0] - save_exceptions
    require(any(name.endswith(".rgba8") for name in outputs),
            "Repeat output inventory differs or lacks raw GPU carrier")
    for name in outputs:
        require((first / name).read_bytes() == (second / name).read_bytes(), f"Repeat differs: {name}")
    # Positive and negative controls for the file-binding oracle, without writes.
    control = {"bytes": (build / "build-report.json").stat().st_size,
               "sha256": digest(build / "build-report.json")}
    hash_row(control, build / "build-report.json")
    try:
        hash_row({**control, "sha256": "0" * 64}, build / "build-report.json")
    except AssertionError:
        pass
    else:
        raise AssertionError("Hash rejection control failed")
    return results, {"sources": len(sources), "bound_files": len(checked),
                     "zero_commands": len(sidecars), "repeat_outputs": len(outputs)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "kit", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    required_sources = {"tools/gameview_cumulative_probe.cpp", "tools/engine_control_probe.hpp",
                        "include/ribterra/view.hpp", "src/client/view.cpp",
                        "include/ribterra/game_art.hpp", "src/client/game_art.cpp",
                        "tools/export_engine_control_kit.py", "tools/export_material_master.py",
                        "tests/gameview_cumulative_evidence_tests.py"}
    results, counts = verify_current_cohort(args.build, args.kit, args.first, args.second,
        required_sources, "cumulative-native-result.json",
        {"cumulative-world-current.save", "steep-world-current.save"})
    for native in results:
        require(native["instances"] >= 8 and native["contacts"] >= 10 and len(native["categories"]) == 8,
                "Cumulative category/contact coverage is incomplete")
        require(native["contact_land_changed_pixels"] > 0 and native["contact_deck_changed_pixels"] > 0 and
                native["direct_only_contact_exact"] and native["hidden_frame_pixel_exact"] and
                native["water_contact_filtered"], "Contact controls did not pass")
    print("cumulative current evidence checks passed; " + ", ".join(f"{key}={value}" for key, value in counts.items()))


if __name__ == "__main__":
    main()
