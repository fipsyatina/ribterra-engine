"""Shared read-only source/build/native bindings for actual weather CPU probes."""
from __future__ import annotations

from pathlib import Path
import math
from gameview_cumulative_evidence_tests import bound_rows, hash_row, load, require


def verify_cohort(build: Path, first: Path, second: Path, label: str,
                  executable_name: str, required_sources: set[str],
                  save_exceptions: set[str], expected_outputs: set[str],
                  varying_measured_fields: set[str]) -> tuple[list[dict], dict]:
    build, first, second = (p.resolve(strict=True) for p in (build, first, second))
    require(first != second, "Two distinct actual native executions required")
    report = load(build / f"weather-{label}-build-report.json")
    require(report["complete"] and Path(report["root"]) == build and report["compiled_units"] == 15,
            "Incomplete or mismatched CPU cohort")
    checked: set[Path] = set()
    bound_rows(report, checked)
    sources = report["source_files"]
    names = {row["relative_path"] for row in sources}
    require(len(names) == len(sources) and required_sources <= names, "Source closure differs")
    require(len(report["source_final"]) == len(sources) and
            {row["relative_path"] for row in report["source_final"]} == names and
            all(row["unchanged"] for row in report["source_final"]), "Source mutated during build")
    for section in ("origin_final", "input_tree_final", "tool_final", "library_final", "protected"):
        require(report[section] and all(row["unchanged"] for row in report[section]),
                f"Builder binding changed: {section}")
    sidecars = set(build.glob("*.log.exit.txt"))
    logs = {Path(str(path).removesuffix(".exit.txt")) for path in sidecars}
    require(len(sidecars) == 20 and all(path.read_text(encoding="utf-8-sig").strip() == "0" for path in sidecars)
            and {Path(row["path"]) for row in report["command_evidence"]} == sidecars | logs,
            "Missing or failing actual command sidecars")
    require(len(report["commands"]) == 20 and all(row["exit_code"] == 0 and row["arguments"] and
            Path(row["working_directory"]) == build for row in report["commands"]), "Command argv/cwd/exit differs")
    require({"/std:c++20", "/O2", "/MT", "/W4", "/WX"} <= set(report["flags"]), "Strict flags differ")
    exe = build / executable_name
    require(Path(report["executable"]["path"]) == exe, "Executable binding differs")
    results, inventories = [], []
    for root in (first, second):
        execution = load(root / "native-execution.json")
        require(execution["actual_subprocess"] and execution["accepted"] and execution["exit_code"] == 0 and
                execution["bound_inputs_unchanged"] and not execution["integrity_errors"], "Native run rejected")
        require(Path(execution["build_root"]) == build and Path(execution["output_root"]) == root and
                execution["actual_argv"] == [str(exe), str(root)], "Actual cohort argv differs")
        bound_rows(execution, checked)
        recorded = execution["native_output_before_runner_evidence"]
        names = {row["relative_path"] for row in recorded}
        actual = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}
        require(len(names) == len(recorded) and actual == names | {"native-execution.json", "execution-output.txt"},
                "Native output ledger incomplete")
        require(names == expected_outputs, "Native output inventory differs from the exact contract")
        require(all(Path(row["path"]) == root / row["relative_path"] for row in recorded), "Output path differs")
        require(sum(p.stat().st_size for p in root.rglob("*") if p.is_file()) <= 128 * 1024**2,
                "Native output budget exceeded")
        native = load(root / f"weather-{label}-report.json")
        require(native["complete"] is True, "Native report incomplete")
        measured = native["measured"]
        require(isinstance(measured, dict) and set(measured) == varying_measured_fields and
                all(type(value) in (int, float) and math.isfinite(value) and value >= 0
                    for value in measured.values()), "Unbound or invalid varying diagnostic")
        results.append(native)
        inventories.append(names)
    require(inventories[0] == inventories[1] and save_exceptions <= inventories[0] and
            all(name.endswith((".save", ".save.bak", ".save.tmp", ".save.bak.tmp")) for name in save_exceptions),
            "Native inventory or exact save exceptions differ")
    result_file = f"weather-{label}-report.json"
    for name in inventories[0] - save_exceptions - {result_file}:
        require((first / name).read_bytes() == (second / name).read_bytes(), f"Derived output differs: {name}")
    require(results[0]["deterministic"] == results[1]["deterministic"], "Deterministic witnesses differ")
    require({key: value for key, value in results[0].items() if key != "measured"} ==
            {key: value for key, value in results[1].items() if key != "measured"},
            "Stable native report schema/version/scope fields differ")
    try:
        hash_row({**report["executable"], "sha256": "0" * 64}, exe)
    except AssertionError:
        pass
    else:
        raise AssertionError("Binding negative control failed")
    return results, {"sources": len(sources), "bound_files": len(checked), "zero_commands": 20,
                     "exact_derived_files": len(inventories[0] - save_exceptions - {result_file})}
