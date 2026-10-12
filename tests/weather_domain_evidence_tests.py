"""Bind actual standalone CPU executions; compare deterministic domain data only."""
from __future__ import annotations

import argparse
import math
from pathlib import Path

from gameview_cumulative_evidence_tests import bound_rows, hash_row, load, require


NEGATIVE_IDS = frozenset("""
CAL-add-overflow CAL-day-overflow CAL-forecast-horizon
COVER-range COVER-positive-remainder-bound COVER-negative-remainder-bound
COVER-lower-outward COVER-upper-outward COVER-phase-temperature COVER-missing-intensity
WORK-rate-low WORK-rate-high WORK-remainder WORK-kind
WIRE-unknown WIRE-version WIRE-profile WIRE-scenario WIRE-offset WIRE-calendar WIRE-snapshot-tick
WIRE-current-kind WIRE-wind-direction WIRE-wind-strength WIRE-temperature WIRE-work-rate WIRE-period
WIRE-period-before-scenario WIRE-period-after-horizon
WIRE-snow-range WIRE-wet-range WIRE-wet-remainder WIRE-wet-outward WIRE-card-day WIRE-card-minmax
WIRE-interval-count WIRE-interval-gap WIRE-interval-overlap WIRE-interval-kind WIRE-inactive-slot
WIRE-summary WIRE-dominant WIRE-has-storm WIRE-hidden-future-storm WIRE-not-earliest-storm
WIRE-storm-crosses-clear WIRE-active-extended-period WIRE-active-hidden
HOST-version HOST-offset HOST-evaluated-tick HOST-noncanonical-temperature HOST-noncanonical-wind
HOST-snow-remainder HOST-wet-remainder HOST-duplicate-resident HOST-unaffected-job HOST-unknown-job
HOST-work-mine-layer HOST-work-target-outside HOST-build-missing-id HOST-work-record-bound
HOST-skip-tick-preserves-state-cache CACHE-outside-window CACHE-invalid-move-preserves
CACHE-scenario-start-barrier KEY-day-zero KEY-purpose
""".split())


def sample_check(sample: dict, tick: int) -> None:
    kind, phase, temperature = (sample[name] for name in ("kind", "precipitation", "temperature_deci_c"))
    require(kind in range(4) and phase in range(3) and -800 <= temperature <= 600,
            "Invalid reported weather sample")
    require(sample["wind_direction"] in range(8) and 0 <= sample["wind_permille"] <= 1000 and
            sample["period_start"] <= tick < sample["period_end"], "Invalid sample wind or period")
    if kind < 2:
        require(phase == 0 and sample["intensity_permille"] == 0, "Dry sample has precipitation")
    else:
        require(phase == (2 if temperature <= 0 else 1) and 1 <= sample["intensity_permille"] <= 1000,
                "Precipitation phase or intensity differs")
    weather_rate = 10000 if kind < 2 else (7500 if phase == 2 else 8000) if kind == 3 else (9000 if phase == 2 else 9500)
    cold_rate = 8500 if temperature <= -200 else 9500 if temperature < -100 else 10000
    rate = max(7500, min(weather_rate, cold_rate))
    reason = 0 if rate == 10000 else 1 if cold_rate < weather_rate else 4 if kind == 3 else 3 if phase == 2 else 2
    require(sample["work_rate_bp"] == rate and
            sample["work_reason"] == reason, "Reported work modifier differs from approved caps")


def remainder_check(value: int, remainder: int) -> bool:
    return (0 <= value <= 1000 and -28800 < remainder < 28800 and
            not (value == 0 and remainder < 0) and not (value == 1000 and remainder > 0))


def component_checks(native: dict) -> None:
    require(native["schema"] == 1 and native["complete"] is True, "Incomplete component outcomes")
    d, measured = native["deterministic"], native["measured"]
    require(d["candidate_version"] == 1 and d["scope"] == "pure host weather domain foundation; candidate tuning",
            "Candidate version or evidence scope differs")
    constants = d["constants"]
    expected_constants = {"ticks_per_day": 28800, "hour_ticks": 1200, "cache_schedules": 6,
                          "forecast_cards": 4, "wire_interval_limit": 8, "generated_kind_interval_limit": 7,
                          "diurnal_offsets": [-25, -30, -35, -40, -45, -50, -50, -40, -25, -10, 5, 20,
                                              30, 40, 45, 50, 45, 35, 20, 5, -5, -10, -15, -20],
                          "mixer_initial_fold": "5249425745415448",
                          "mixer_constants": ["9e3779b97f4a7c15", "bf58476d1ce4e5b9", "94d049bb133111eb"],
                          "purpose_ids": list(range(1, 12)),
                          "candidate_merge_wind_convention": "earliest equal-kind/intensity period wind retained; connected generic events coalesced across midnight; background periods remain day-local"}
    require(constants == expected_constants, "Frozen candidate constants differ")
    expected_mix = ((0, "e220a8397b1dcdaf"), (1, "910a2dec89025cc1"))
    require(d["mixer_goldens"] == [{"input": value, "expected_hex": digest, "value": int(digest, 16)}
                                   for value, digest in expected_mix], "Literal mixer goldens differ")
    calendar = d["calendar_goldens"]
    require(len(calendar) == 6 and calendar[:5] == [
        {"simulation_tick": tick, "day": 2 if tick == 28800 else 1, "clock_minute": minute, "season": 0}
        for tick, minute in zip((0, 7200, 14400, 21600, 28800), (360, 720, 1080, 0, 360))] and
        calendar[5] == {"simulation_tick": 0, "calendar_tick": 864000, "day": 31, "clock_minute": 360},
        "Typed calendar origin or first-winter offset differs")
    cover = d["cover_goldens"]
    for name, expected in (("snow_day", (500, 0, 0, 0)), ("rain_day", (10, 1000, 0, 0)),
                           ("dry_day", (100, 780, 0, 0)), ("signed_step", (500, 500, -800, -120)),
                           ("domain_value_copy_continuation", (1000, 170, 0, -9))):
        require(cover[name] == dict(zip(("snow", "wetness", "snow_remainder", "wetness_remainder"), expected)),
                f"Signed fixed-point cover golden differs: {name}")
    require(cover["actual_save_roundtrip"] is False, "Domain value copy was presented as a save codec test")
    work = d["work_goldens"]
    require(len(work) == 6 and {row["rate_bp"] for row in work} == {10000, 9500, 9000, 8500, 8000, 7500},
            "Work controls missing or duplicated")
    for row in work:
        require(row["ticks"] == 100 and row["steps"] == row["rate_bp"] // 100 and row["final_remainder"] == 0 and
                row["midpoint_remainder"] == (37 * row["rate_bp"]) % 10000 and
                row["domain_value_copy_continuation"] is True, "Work golden or remainder continuation differs")
    negatives = d["invalid_controls"]
    require(len(negatives) == len(NEGATIVE_IDS) == 68 and {row["id"] for row in negatives} == NEGATIVE_IDS and
            all(row["rejected"] is True for row in negatives), "Exact invalid control set is incomplete")
    boundaries = d["boundary_samples"]
    expected_boundaries = {(seed, (day - 1) * 28800 + offset) for seed in (70421, 0, 4294967295)
                           for day in (1, 10, 11, 30, 31, 40, 41) for offset in (0, 599, 600, 1199, 1200, 28799)}
    require(len(boundaries) == 126 and {(row["seed"], row["calendar_tick"]) for row in boundaries} == expected_boundaries,
            "Boundary/rebuild/rollback cohort differs")
    for row in boundaries:
        sample_check(row["sample"], row["calendar_tick"])
    expected_keys = (5851049354480010929, 3825483856398810768, 13195172845923090279,
                     3952134337344217059, 8099500713818125661, 87010721489369,
                     2761471186849154308, 17531926826607087551, 10142257412091890343)
    require(d["seeded_key_goldens"] == [{"seed": 286479, "day": 31, "purpose": i + 1,
            "period": 4 if i >= 4 else 0, "expected": value, "actual": value}
            for i, value in enumerate(expected_keys)], "Independent literal seeded key vectors differ")
    carry = d["generic_carryover_golden"]
    require(carry["seed"] == 286479 and carry["scenario"] == 0 and carry["days"] == [31, 32] and
            carry["source_events"] == [
                {"start": 891173, "end": 897792, "intensity": 837, "wind_direction": 7, "wind": 951},
                {"start": 893382, "end": 900539, "intensity": 837, "wind_direction": 0, "wind": 909}] and
            carry["union_start"] == 891173 and carry["union_end"] == 900539 and
            carry["retained_wind"] == 951 and carry["retained_direction"] == 7 and
            carry["midnight_one_tick_advance_preserves_period"] is True and
            carry["candidate_literal_regression"] is True and carry["certified_shipping_seed"] is False,
            "Generic cross-midnight carryover fixture or evidence scope differs")
    carry_ticks = (891172, 891173, 891772, 891773, 892799, 892800, 893381, 893382, 900538, 900539)
    carry_temperatures = (-73, -73, -134, -135, -135, -135, -131, -131, -64, -64)
    require(len(carry["vectors"]) == 10, "Carryover literal sample cohort incomplete")
    for i, row in enumerate(carry["vectors"]):
        kind = 1 if i == 0 else 0 if i == 9 else 3
        sample_check(row["sample"], carry_ticks[i])
        require(row["tick"] == carry_ticks[i] and row["expected_temperature_deci_c"] == carry_temperatures[i] and
                row["sample"]["temperature_deci_c"] == carry_temperatures[i] and
                row["expected_kind"] == row["sample"]["kind"] == kind, "Literal carryover temperature/kind differs")
        if kind == 3:
            require(all(row["sample"][name] == value for name, value in {
                "period_start": 891173, "period_end": 900539, "intensity_permille": 837,
                "wind_permille": 951, "wind_direction": 7, "precipitation": 2,
                "work_rate_bp": 7500, "work_reason": 4}.items()), "Carryover period/wind/phase changed")
    forecast = d["forecast_checks"]
    require(forecast == {"exact_realized_minmax_days": 30, "exact_realized_phase_days": 30,
                        "rollback_rebuild_seed_count": 3, "runtime_current_cover_source": True,
                        "active_storm_pair_exact_period": True,
                        "future_storm_structural_limit": "kind-only cards cannot prove distinct-intensity period end within adjacent Storm run"},
            "Forecast realization controls or structural limit differ")
    trial = d["candidate_30_day"]
    require(trial["seed"] == 70421 and trial["observed_ticks"] == 864000 and len(trial["days"]) == 30 and
            len(trial["kind_ticks"]) == 4 and all(isinstance(n, int) and n >= 0 for n in trial["kind_ticks"]) and
            sum(trial["kind_ticks"]) == 864000 and trial["rain_ticks"] >= 0 and trial["snow_ticks"] >= 0 and
            trial["rain_ticks"] + trial["snow_ticks"] == sum(trial["kind_ticks"][2:]),
            "Thirty-day exhaustive observation counts differ")
    require(trial["domain_value_reconstruction_continuation"] is True and
            trial["actual_world_save_wire_integration"] is False and trial["tuning_acceptance"] == "OPEN",
            "Component evidence was promoted to integration or tuning acceptance")
    minima, maxima = [], []
    for day, row in enumerate(trial["days"], 1):
        card = row["forecast"]
        require(card["day"] == day and -800 <= card["minimum_deci_c"] <= card["maximum_deci_c"] <= 600 and
                card["interval_count"] == len(card["intervals"]) and 1 <= card["interval_count"] <= 7,
                "Forecast day, extrema or interval count invalid")
        minima.append(card["minimum_deci_c"])
        maxima.append(card["maximum_deci_c"])
        duration = [0, 0, 0, 0]
        cursor = 0
        for interval in card["intervals"]:
            require(interval["kind"] in range(4) and interval["start"] == cursor and cursor < interval["end"] <= 28800,
                    "Forecast interval gap, overlap or kind invalid")
            duration[interval["kind"]] += interval["end"] - cursor
            cursor = interval["end"]
        require(cursor == 28800 and card["dominant_kind"] == max(range(4), key=lambda kind: (duration[kind], kind)) and
                card["has_storm"] == (duration[3] > 0) and card["precipitation_summary"] in range(4) and
                (card["precipitation_summary"] == 0) == (sum(duration[2:]) == 0), "Forecast summary differs")
        require(remainder_check(row["end_snow"], row["end_snow_remainder"]) and
                remainder_check(row["end_wetness"], row["end_wetness_remainder"]), "Cover remainder or clamp invalid")
    require(trial["minimum_deci_c"] == min(minima) and trial["maximum_deci_c"] == max(maxima),
            "Thirty-day extrema differ from exact daily extrema")
    winter = d["first_winter_seed_check"]
    require(winter["seed"] == 70421 and winter["offset"] == 864000 and
            873600 <= winter["start"] <= 878400 and 4800 <= winter["duration"] <= 7200 and
            winter["end"] - winter["start"] == winter["duration"] and
            winter["all_ticks_snow"] == (winter["maximum_storm_temperature_deci_c"] <= 0) and
            winter["certified_shipping_seed"] is False and winter["seed_acceptance"] == "OPEN",
            "First-winter boundaries or seed acceptance scope differ")
    for name, tick, storm in (("before", winter["start"] - 1, False), ("at_start", winter["start"], True),
                              ("before_end", winter["end"] - 1, True), ("at_end", winter["end"], False)):
        sample_check(winter[name], tick)
        require((winter[name]["kind"] == 3) == storm, "Forced winter event endpoints invalid")
    require(d["open_delivery"] == ["World authority and typed calendar call-site migration",
        "World Activity key matching and cancellation", "actual save/checksum round trips",
        "actual full/delta/split wire and ClientSession", "HUD unknown/current/forecast",
        "weather FX assets and renderer", "first-winter seed certification and tuning acceptance",
        "full simulation 1-percent overhead"], "Integration limitations omitted")
    cold, rollover = measured["cold_cache_samples_ms"], measured["day_rollover_ms"]
    require(len(cold) == len(rollover) == 100 and all(math.isfinite(v) and v >= 0 for v in cold + rollover),
            "Raw timing cohort missing or invalid")
    require(measured["cold_cache_init_ms"] == sorted(cold)[94] and measured["day_rollover_p95_ms"] == sorted(rollover)[94] and
            measured["cold_samples_per_build"] == 115200 and measured["roll_samples_per_build"] == 28800 and
            measured["cold_init_total_under10ms"] == (sorted(cold)[94] <= 10) and
            measured["pure_forecast_day_10ms_goal_met"] == (sorted(rollover)[94] <= 10) and
            measured["allocation_scope"] == "global operator-new instrumentation; pure domain, no allocator bypass/platform heap profiling",
            "Measured p95, pure goals or allocator scope differ")
    require(measured["steady_ticks"] == 10000 and math.isfinite(measured["steady_elapsed_ms"]) and
            measured["steady_elapsed_ms"] >= 0 and
            math.isclose(measured["steady_ns_per_tick"], measured["steady_elapsed_ms"] * 100, rel_tol=1e-12) and
            all(measured[name] == 0 for name in ("day_rollover_allocations", "day_rollover_allocated_bytes",
                                               "steady_allocations", "steady_allocated_bytes")) and
            measured["measurement_witness"] > 0 and
            measured["scope"] == "pure domain; no full simulation/renderer/network overhead measured",
            "Allocation evidence, steady timing or measured scope differs")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    build, first, second = (path.resolve(strict=True) for path in (args.build, args.first, args.second))
    require(first != second, "Two distinct actual CPU executions required")
    report = load(build / "weather-build-report.json")
    require(report["complete"] and Path(report["root"]) == build and report["compiled_units"] == 2,
            "Incomplete or mismatched domain build")
    checked: set[Path] = set()
    bound_rows(report, checked)
    sources = report["source_files"]
    names = {source["relative_path"] for source in sources}
    required = {"src/sim/weather.cpp", "tools/weather_domain_probe.cpp", "include/ribterra/weather.hpp",
                "include/ribterra/seasons.hpp", "include/ribterra/world_grid.hpp",
                "tools/build_weather_domain_isolated.ps1", "tools/run_weather_domain_native.py",
                "tests/weather_domain_evidence_tests.py", "tests/gameview_cumulative_evidence_tests.py",
                "build/weather-carryover-derivation-2026-10-08.json", "build/weather-folded-key-derivation-2026-10-08.json"}
    require(len(names) == len(sources) and required <= names, "Incomplete or duplicate domain source closure")
    require({source["relative_path"] for source in report["source_final"]} == names and
            len(report["source_final"]) == len(sources) and
            all(source["unchanged"] for source in report["source_final"]), "Domain source mutated during build")
    require(report["protected"] and all(row["unchanged"] for row in report["protected"]),
            "Launcher binding changed")
    sidecars = set(build.glob("*.log.exit.txt"))
    logs = {Path(str(path).removesuffix(".exit.txt")) for path in sidecars}
    require(len(sidecars) == 3 and {Path(row["path"]) for row in report["command_evidence"]} == sidecars | logs and
            all(path.read_text(encoding="utf-8-sig").strip() == "0" for path in sidecars),
            "Actual compile/link commands missing or failed")
    require({"/std:c++20", "/O2", "/MT", "/W4", "/WX"} <= set(report["flags"]), "Strict flags differ")
    native_results = []
    for root in (first, second):
        execution = load(root / "native-execution.json")
        require(execution["actual_subprocess"] and execution["accepted"] and execution["exit_code"] == 0 and
                execution["bound_inputs_unchanged"] and not execution["integrity_errors"], "CPU execution rejected")
        require(Path(execution["build_root"]) == build and Path(execution["output_root"]) == root and
                execution["actual_argv"] == [str(build / "WeatherDomainProbe.exe"), str(root)],
                "Actual CPU arguments/cohort differ")
        bound_rows(execution, checked)
        files = execution["native_output_before_runner_evidence"]
        require(len(files) == 1 and files[0]["relative_path"] == "weather-domain-report.json" and
                Path(files[0]["path"]) == root / "weather-domain-report.json", "Unexpected native domain output ledger")
        require({p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()} ==
                {"weather-domain-report.json", "execution-output.txt", "native-execution.json"},
                "Domain output inventory differs")
        native = load(root / "weather-domain-report.json")
        component_checks(native)
        native_results.append(native)
    require(native_results[0]["deterministic"] == native_results[1]["deterministic"],
            "Candidate domain deterministic outputs differ")
    # Check the binding oracle itself without writing or modifying any artifact.
    control = report["executable"]
    try:
        hash_row({**control, "sha256": "0" * 64}, Path(control["path"]))
    except AssertionError:
        pass
    else:
        raise AssertionError("Negative binding oracle failed")
    print(f"weather domain checks passed; sources={len(sources)}, bound_files={len(checked)}, zero_commands=3")


if __name__ == "__main__":
    main()
