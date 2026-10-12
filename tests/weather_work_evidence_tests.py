"""Read-only artifact bindings and independent work-rate arithmetic checks."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
from gameview_cumulative_evidence_tests import require
from weather_cpu_evidence import verify_cohort


POSITIVE = {
    "DOMAIN-real-cold", "GATHER-woodcut20", "GATHER-mine20", "GATHER-forage20",
    "BUILD-cold20", "MINE-underground20", "WAIT-night", "WAIT-movement", "WAIT-path",
    "RESET-same-key-replace", "RESET-stop", "RESET-manual-cancel", "RESET-construction-cancel",
    "YIELD-earned-retry", "PATH-correlated-replan-timeout", "PATH-obsolete-candidate-rejected",
    "SAVE-gather-200", "SAVE-build-200",
}
INVALID = {"BIND-" + name for name in (
    "resident-missing", "resident-zero", "job-unaffected", "job-live-mismatch",
    "target-live-mismatch", "target-layer", "gather-building", "build-id-zero",
    "build-id-missing", "build-id-live-mismatch", "duplicate", "unsorted",
    "remainder-overflow", "dead-actor", "route-missing", "activity-live-mismatch")}
SAVES = {"work-gather.save", "work-build.save"}


def ids(rows: list[dict], expected: set[str]) -> None:
    require(len(rows) == len(expected) and {row["id"] for row in rows} == expected,
            "Exact control coverage differs")


def work_rows(rows: list[dict], nonzero: bool = False) -> None:
    residents = [row["resident"] for row in rows]
    require(residents == sorted(set(residents)), "Work rows are not sorted/unique")
    for row in rows:
        require(row["resident"] > 0 and len(row["target"]) == 3 and row["target"][2] == 0 and
                row["job"] in (2, 3, 4, 5) and 0 <= row["remainder"] < 10000 and
                (row["building"] > 0 if row["job"] == 5 else row["building"] == 0), "Invalid work row")
    if nonzero:
        require(rows and all(row["remainder"] > 0 for row in rows), "Vacuous live work control")


def twenty(case: dict, cold_tick: int, build: bool = False, underground: bool = False) -> None:
    # Cumulative fractions independently determine each admitted step.
    remainder = [(8500 * n) % 10000 for n in range(1, 21)]
    admitted = [(8500 * n) // 10000 > (8500 * (n - 1)) // 10000 for n in range(1, 21)]
    expected_steps = [True] * 20 if underground else admitted
    require(case["rate_bp"] == 8500 and case["eligible_ticks"] == 20 and
            case["baseline_speed_bp"] == 10000 and case["advance_count"] == sum(expected_steps) and
            case["steps"] == expected_steps and
            case["remainders"] == ([None] * 20 if underground else remainder), "Fixed-point gate differs")
    require(case["first4_advances"] == expected_steps[:4] and
            case["first4_remainders"] == ([] if underground else remainder[:4]), "Literal first-four vector differs")
    before = .25 if build else 0
    after = before + sum(expected_steps) * (.01 if build else .025)
    require(math.isclose(case["progress_before"], before, abs_tol=1e-5) and
            math.isclose(case["progress_after"], after, abs_tol=1e-5) and
            case["integer_xp_before"] == case["integer_xp_after"] == 120 and
            case["useful_work_units"] == (1700 if build else 0), "Work/XP credit differs")
    practice = case["initial_practice"]
    require(practice["xp"] == 120 and practice["remainder"] == 0 and
            practice["familiarity_debt"] == 0 and practice["last_tick"] == cold_tick - 1 and
            len(case["practices"]) == 20,
            "Practice baseline differs")
    earned = 0
    for index, (advance, current) in enumerate(zip(expected_steps, case["practices"], strict=True)):
        require(current["xp"] == 120, "Short fixture changed integer XP")
        if not advance or not build:
            require(current == practice, "Denied tick or incomplete gather credited practice")
        else:
            earned += 100
            require(current["remainder"] == earned and current["last_tick"] == cold_tick + index and
                    current["familiarity_debt"] == 0,
                    "Build useful-work credit differs")
        practice = current
    require(case["no_outdoor_record"] is underground, "Underground scope differs")
    work_rows(case["rows"])
    require(case["rows"] == [] if underground else
            len(case["rows"]) == 1 and case["rows"][0]["remainder"] == 0, "Final keyed row differs")


def checks(result: dict, root: Path) -> None:
    require(result["schema"] == 1 and result["versions"] ==
            {"save": 9, "protocol": 23, "rules": 19, "generator": 8}, "Version contract differs")
    d = result["deterministic"]
    ids(d["controls"], POSITIVE)
    require(all(row["passed"] is True for row in d["controls"]), "Positive control failed")
    cold = d["cold_candidate"]
    require(0 <= cold["seed"] <= 4095 and cold["seed_bound"] == 4095 and cold["days"] == [34, 35, 36, 37] and
            cold["day"] in cold["days"] and cold["window_ticks"] == 20 and cold["checked_windows"] > 0 and
            cold["scenario"] == 0 and cold["calendar_tick"] // 28800 + 1 == cold["day"] and
            0 <= cold["calendar_tick"] % 28800 < 14400 - 20 and len(cold["samples"]) == 20,
            "Real daytime cold witness differs")
    for index, sample in enumerate(cold["samples"]):
        require(sample["tick"] == cold["calendar_tick"] + index and -800 <= sample["temperature"] <= -200 and
                sample["rate"] == 8500 and sample["reason"] == 1 and
                sample["period_start"] <= sample["tick"] < sample["period_end"], "Cold sample differs")
    require(set(d["surface"]) == {"woodcut", "mine", "forage", "build"}, "Surface coverage differs")
    for name, case in d["surface"].items():
        twenty(case, cold["calendar_tick"], build=name == "build")
        require(case["rows"] == [{"resident": 1, "job": {"woodcut": 2, "mine": 3, "forage": 4, "build": 5}[name],
                "target": [26, 26, 0] if name == "build" else [22, 19, 0],
                "building": 2 if name == "build" else 0, "remainder": 0}], "Literal Activity key differs")
    twenty(d["mine"], cold["calendar_tick"], underground=True)
    require(set(d["freeze"]) == {"night", "movement", "path"}, "Wait coverage differs")
    for name, case in d["freeze"].items():
        work_rows(case["before_rows"], True)
        require(case["before_rows"] == case["after_rows"] and case["before_rows"][0]["remainder"] == 7000 and
                case["ticks"] == (1 if name == "path" else 4), "Wait spent keyed remainder")
    move = d["freeze"]["movement"]
    require(move["position_before"] != move["position_after"], "Movement wait did not move")
    path = d["freeze"]["path"]
    require(path["batch_id"] > 0 and path["authored_connected_cells"] == 8280 and
            path["planner_budget_per_tick"] == 4096, "Pending-path fixture is vacuous")
    reset = d["resets"]
    ids(reset["witnesses"], {id for id in POSITIVE if id.startswith("RESET-")})
    for witness in reset["witnesses"]:
        work_rows(witness["before_rows"], True)
        require(witness["public_commands"] == 1, "Reset did not use an actual command")
        if witness["id"] == "RESET-same-key-replace":
            require(witness["rows_immediately_after_command"] == [] and
                    0 < witness["actual_ticks_to_new_key"] <= 20, "Same-key replacement retained prior accumulator")
            work_rows(witness["fresh_actual_rows"], True)
        else:
            require(witness["after_rows"] == [] and witness["actual_ticks_after_command"] == 0, "Cancellation retained key")
    earned = d["earned_yield"]
    work_rows(earned["earned_row"])
    require(len(earned["earned_row"]) == 1 and earned["practice_before"]["xp"] == 120 and
            earned["practice_after"]["xp"] == 121 and
            all(earned[name]["remainder"] == earned[name]["familiarity_debt"] == 0
                for name in ("practice_before", "practice_after")), "Earned retry practice differs")
    require(math.isclose(earned["declared_partial_progress_before"], .58, abs_tol=1e-5) and
            earned["actual_ticks_to_earned_progress"] == 20 and earned["earned_progress"] == 1 and
            earned["earned_row"][0]["remainder"] == 0 and earned["blocked_retry_frozen"] is True and
            earned["blocked_retry_ticks"] == earned["successful_retry_ticks"] == 1 and
            earned["explicit_restored_logistics_limit"] == 1 and earned["retained_retry_remainder"] == 0 and
            earned["would_deny_rate"] == 8500 and earned["would_deny_from_zero"] is True and
            earned["carried_wood"] == 3 and earned["xp_after"] == 121, "Earned retry credit differs")
    failed = d["failed_replan"]
    work_rows(failed["before_rows"], True)
    work_rows(failed["newer_rows"], True)
    require(failed["timeout_actual_ticks"] == 1 and 0 < failed["newer_actual_ticks"] <= 20 and failed["after_rows"] == [] and
            failed["timeout_progress_unchanged"] and failed["timeout_practice_unchanged"] and
            failed["obsolete_candidate_unchanged"] and failed["obsolete_candidate_error"] ==
            "Path resident reservation is invalid.", "Failed/obsolete path transaction differs")
    ids(d["invalid_controls"], INVALID)
    for row in d["invalid_controls"]:
        work_rows(row["before_rows"], True)
        require(row["simulation_tick"] > 0 and row["rejected"] and row["unchanged"] and row["error"],
                "Live candidate rejection is vacuous or failed")
    require(set(d["persistence"]) == {"gather", "build"}, "Persistence coverage differs")
    for name, case in d["persistence"].items():
        work_rows(case["source_rows"], True)
        require(case["source_rows"] == [{"resident": 1, "job": 5 if name == "build" else 2,
                "target": [26, 26, 0] if name == "build" else [22, 19, 0],
                "building": 2 if name == "build" else 0, "remainder": 7000}], "Loaded binding key differs")
        require(case["filename"] == f"work-{name}.save" and case["source_rows"][0]["remainder"] == 7000 and
                case["source_tick"] == cold["calendar_tick"] + 1 and case["continuation_ticks"] == 200 and
                len(case["checksums"]) == len(case["work_rows"]) == 200 and
                case["checksums"][-1] == case["final_checksum"] and case["checksum_same"] and
                case["loaded_binding_exact"] and case["actual_save_load"], "Nonzero save/load continuation differs")
        for rows in case["work_rows"]:
            work_rows(rows)
    require(len(result["open_delivery"]) == 7 and
            result["measured"]["output_bytes_before_report"] == sum((root / name).stat().st_size for name in SAVES),
            "Checkpoint scope/output diagnostic differs")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    results, counts = verify_cohort(args.build, args.first, args.second, "work", "WeatherWorkProbe.exe",
        {"src/sim/world.cpp", "src/sim/weather.cpp", "src/sim/world_save.cpp", "tools/weather_work_probe.cpp",
         "tools/build_weather_work_isolated.ps1", "tools/run_weather_work_native.py",
         "tests/weather_work_evidence_tests.py", "tests/weather_cpu_evidence.py"},
        SAVES, SAVES | {"weather-work-report.json"}, {"cold_search_ms", "total_ms", "output_bytes_before_report"})
    for result, root in zip(results, (args.first, args.second), strict=True):
        checks(result, root.resolve(strict=True))
    print("weather work evidence checks passed; " + ", ".join(f"{key}={value}" for key, value in counts.items()))


if __name__ == "__main__":
    main()
