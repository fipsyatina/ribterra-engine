"""Read-only bindings and semantic controls for two actual CPU authority runs."""
from __future__ import annotations

import argparse
from pathlib import Path
from gameview_cumulative_evidence_tests import bound_rows, hash_row, load, require


def checks(result: dict) -> None:
    require(result["schema"] == 1 and result["complete"] is True, "Authority probe incomplete")
    require(result["versions"] == {"protocol": 23, "rules": 19, "generator": 8, "save": 9},
            "Climate schema/version contract differs")
    world = result["world"]
    require(world["standard_initial"]["simulation_tick"] == 0 and
            world["standard_initial"]["calendar_tick"] == 0 and
            world["winter_initial"]["simulation_tick"] == 0 and
            world["winter_initial"]["calendar_tick"] == 864000 and
            world["winter_initial"]["snow"] == 650 and world["initial_food"] == 1200,
            "Simulation/calendar initial split or physical provisioning differs")
    require(world["world_single_step_ticks"] == 1201 and world["restore_continuation_ticks"] == 200 and
            world["continued_weather"]["simulation_tick"] == 1402 and
            world["continued_weather"]["calendar_tick"] == 865402 and
            world["wetness_changes_checksum"] is True, "Actual climate continuation/checksum differs")
    require(world["provisioning"] == [
        {"population": 0, "food": 100, "capacity": 10000},
        {"population": 83, "food": 9960, "capacity": 10080},
        {"population": 1500, "food": 180000, "capacity": 180120}], "Physical capacity boundaries differ")
    require(world["actual_storm_save_tick"] == 12650 and world["actual_storm_snow_remainder"] != 0 and
            world["actual_storm_wet_remainder"] != 0 and
            world["nonzero_signed_remainder_continuation_ticks"] == 200 and
            world["actual_storm_weather"]["simulation_tick"] == 12850,
            "Actual nonzero signed storm remainder restore is vacuous or differs")
    invalid = world["import_rejections"]
    require({row["id"] for row in invalid} == {
        "tick0-snow-history", "evaluated-tick", "scenario-offset", "algorithm-version",
        "current-temperature", "snow-range", "wet-remainder-range", "missing-activity-key"} and
        len(invalid) == 8 and all(row["rejected"] and row["unchanged"] and row["error"] for row in invalid),
        "Climate candidate import rejection controls differ")
    persistence = result["persistence"]
    guards = persistence["guards"]
    require({row["id"] for row in guards} == {"old-primary", "old-backup", "malformed-current-backup",
        "stage-collision", "backup-stage-collision", "locked-primary-promotion"} and len(guards) == 6 and
        all(row["rejected"] and row["unchanged"] and row["error"] for row in guards) and
        persistence["old_load_rejected_before_decode"], "Save guard/retention controls differ")
    locked = next(row for row in guards if row["id"] == "locked-primary-promotion")
    require(locked["stages_retained"], "Actual failed primary publication did not retain staging")
    transport = result["transport"]
    require(all(transport[key] is True for key in ("full_roundtrip", "delta_replacement",
        "legal_weather_header_mismatch_rejected", "partial_unchanged", "timeout_unchanged",
        "waiting_lobby_weather")) and transport["split_parts"] > 1 and
        0 < transport["maximum_encoded_packet_bytes"] <= 256 * 1024,
        "Full/delta/split transactional weather controls differ")
    require(transport["full_weather"]["simulation_tick"] == 0 and
            transport["delta_weather"]["simulation_tick"] == 1, "Wire weather tick differs")
    client = result["client"]
    require(all(client[key] is True for key in ("local_typed_calendar", "remote_before_snapshot_unknown",
        "actual_loopback_same_host_weather", "remote_disconnected_unknown")) and
        client["joined_weather"] == transport["full_weather"], "Client authority/readiness differs")
    require(len(result["open_delivery"]) == 6, "Checkpoint overstates remaining delivery")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    build, first, second = (p.resolve(strict=True) for p in (args.build, args.first, args.second))
    require(first != second, "Two distinct actual executions required")
    report = load(build / "weather-authority-build-report.json")
    require(report["complete"] and Path(report["root"]) == build and report["compiled_units"] == 15,
            "Authority build incomplete or cohort differs")
    checked: set[Path] = set()
    bound_rows(report, checked)
    sources = report["source_files"]
    names = {source["relative_path"] for source in sources}
    require(len(names) == len(sources) and {"src/sim/world.cpp", "src/sim/weather.cpp", "src/sim/world_save.cpp",
        "src/net/protocol.cpp", "src/net/network_session.cpp", "src/client/client_session.cpp",
        "schemas/world_save.fbs", "schemas/network.fbs", "include/ribterra/weather.hpp",
        "tools/weather_authority_probe.cpp", "tools/run_weather_authority_native.py",
        "tests/weather_authority_evidence_tests.py", "tests/gameview_cumulative_evidence_tests.py"} <= names,
        "Authority source closure incomplete")
    require(len(report["source_final"]) == len(sources) and
            {source["relative_path"] for source in report["source_final"]} == names and
            all(source["unchanged"] for source in report["source_final"]), "Source changed during build")
    require(report["protected"] and all(row["unchanged"] for row in report["protected"]), "Launchers changed")
    sidecars = set(build.glob("*.log.exit.txt"))
    require(len(sidecars) == 20 and all(path.read_text(encoding="utf-8-sig").strip() == "0" for path in sidecars) and
            {Path(row["path"]) for row in report["command_evidence"]} == sidecars |
            {Path(str(path).removesuffix(".exit.txt")) for path in sidecars}, "Build command evidence differs")
    require({"/std:c++20", "/O2", "/MT", "/W4", "/WX"} <= set(report["flags"]), "Strict flags differ")
    results = []
    inventories = []
    for root in (first, second):
        execution = load(root / "native-execution.json")
        require(execution["actual_subprocess"] and execution["accepted"] and execution["exit_code"] == 0 and
                execution["bound_inputs_unchanged"] and not execution["integrity_errors"], "Native run rejected")
        require(Path(execution["build_root"]) == build and Path(execution["output_root"]) == root and
                execution["actual_argv"] == [str(build / "WeatherAuthorityProbe.exe"), str(root)],
                "Actual executable/argv differ")
        bound_rows(execution, checked)
        recorded = execution["native_output_before_runner_evidence"]
        names = {row["relative_path"] for row in recorded}
        require(len(names) == len(recorded) and
                {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()} == names |
                {"execution-output.txt", "native-execution.json"}, "Native output ledger incomplete")
        for row in recorded:
            require(Path(row["path"]) == root / row["relative_path"], "Output path binding differs")
        native = load(root / "weather-authority-report.json")
        checks(native)
        results.append(native)
        inventories.append(names)
    require(inventories[0] == inventories[1] and results[0] == results[1], "Deterministic CPU results differ")
    # Each random World UUID is bound against its own run, while all report
    # semantics must match. Enumerate every excluded save/staging file exactly.
    save_files = {"authority-current.save", "authority-current.save.bak", "authority-storm.save", "guard-source.save",
        "old-primary.save", "old-backup.save", "old-backup.save.bak", "malformed-current-backup.save",
        "malformed-current-backup.save.bak", "stage-collision.save", "backup-stage-collision.save",
        "locked-primary.save", "locked-primary.save.bak", "locked-primary.save.tmp", "locked-primary.save.bak.tmp"}
    require(inventories[0] == save_files | {"stage-collision.save.tmp", "backup-stage-collision.save.bak.tmp",
        "weather-authority-report.json"}, "Unexpected file exception/inventory")
    for name in inventories[0] - save_files:
        require((first / name).read_bytes() == (second / name).read_bytes(), f"Derived output differs: {name}")
    try:
        hash_row({**report["executable"], "sha256": "0" * 64}, Path(report["executable"]["path"]))
    except AssertionError:
        pass
    else:
        raise AssertionError("Binding negative control failed")
    print(f"weather authority checks passed; sources={len(sources)}, bound_files={len(checked)}, zero_commands=20")


if __name__ == "__main__":
    main()
