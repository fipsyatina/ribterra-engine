"""Read-only bindings and independent first-storm/ration boundary arithmetic."""
from __future__ import annotations

import argparse
from pathlib import Path
from gameview_cumulative_evidence_tests import require
from weather_cpu_evidence import verify_cohort


POSITIVE = {"SEED-all-snow", "ORIGIN-no-replay", "STOCK-physical1200", "WORLD-canonical24000",
            "RATIONS-120", "SAVE-before200", "SAVE-start200", "SAVE-end200"}
SAVES = {"winter-before.save", "winter-start.save", "winter-end.save"}


def population(rows: list[dict], stopped: bool) -> None:
    require([row["id"] for row in rows] == list(range(1, 11)) and
            all(row["health"] == 100 and row["hunger"] == 0 for row in rows), "Ten healthy consumers differ")
    if stopped:
        require(all(row["job"] == 0 and row["automation_enabled"] is False for row in rows),
                "Stopped actors resumed production")


def climate_weather(climate: dict, weather: dict, tick: int) -> None:
    require(climate["algorithm"] == weather["algorithm"] == 1 and
            climate["profile"] == weather["profile"] == 1 and climate["scenario"] == weather["scenario"] == 1 and
            climate["offset"] == weather["offset"] == 864000 and weather["known"] is True and
            climate["simulation_tick"] == weather["simulation_tick"] == tick and
            weather["calendar_tick"] == 864000 + tick and climate["current"] == weather["current"] and
            climate["work_rows"] == [], "Authoritative calendar/private current differs")
    for key in ("snow_cover_permille", "wetness_permille", "wetness_remainder"):
        require(climate[key] == weather[key], "Public/private cover differs")
    require(0 <= climate["snow_cover_permille"] <= 1000 and 0 <= climate["wetness_permille"] <= 1000 and
            -28800 < climate["snow_cover_remainder"] < 28800 and
            -28800 < climate["wetness_remainder"] < 28800, "Private cover range differs")
    day = weather["calendar_tick"] // 28800 + 1
    require(len(weather["forecast"]) == 4 and [card["day"] for card in weather["forecast"]] == list(range(day, day + 4)),
            "Four future calendar cards differ")
    for card in weather["forecast"]:
        intervals = card["intervals"]
        require(1 <= len(intervals) <= 8 and intervals[0][0] == 0 and intervals[-1][1] == 28800 and
                all(0 <= a < b <= 28800 and 0 <= kind <= 3 for a, b, kind in intervals) and
                all(left[1] == right[0] and left[2] != right[2] for left, right in zip(intervals, intervals[1:])),
                "Canonical interval partition differs")


def snapshot(case: dict, tick: int, stopped: bool = True) -> None:
    require(case["simulation_tick"] == tick and case["stored_food"] == 1200 - 10 * (tick // 200),
            "Actual boundary ration stock differs")
    population(case["population"], stopped)
    climate_weather(case["climate"], case["weather"], tick)


def checks(result: dict, root: Path) -> None:
    require(result["schema"] == 1 and result["versions"] ==
            {"save": 9, "protocol": 23, "rules": 19, "generator": 8}, "Version contract differs")
    d = result["deterministic"]
    controls = d["controls"]
    require(len(controls) == 8 and {row["id"] for row in controls} == POSITIVE and
            all(row["passed"] is True for row in controls), "Exact eight controls differ")
    invalid = d["invalid_controls"]
    require(len(invalid) == 1 and invalid[0]["id"] == "SEED-70421-rain" and invalid[0]["rejected"] is True,
            "Known Rain seed negative missing")
    negative = invalid[0]
    require(negative["first_rejection_tick"] == 876050 and negative["sample"]["temperature_deci_c"] == 45 and
            negative["sample"]["kind"] == 3 and negative["sample"]["phase"] == 1 and
            negative["full_storm"]["seed"] == 70421 and negative["full_storm"]["rain_tick_count"] > 0 and
            negative["full_storm"]["start_calendar_tick"] == 876050 and
            negative["full_storm"]["end_calendar_tick"] == 882343,
            "Literal 70421 Rain counterexample differs")
    search = d["search"]
    selected = search["selected"]
    require(search["first_seed"] == 0 and search["last_seed"] == 4095 and 0 <= selected["seed"] <= 4095 and
            search["tried_count"] == selected["seed"] + 1 and
            [row["seed"] for row in search["rejected_candidates"]] == list(range(selected["seed"])) and
            search["certified_first_storm_snow"] is True and search["shipping_startup_selected"] is False,
            "Bounded ascending search or delivery scope differs")
    for candidate in search["rejected_candidates"]:
        require(candidate["sample"]["temperature_deci_c"] > 0 and candidate["sample"]["phase"] == 1 and
                candidate["sample"]["kind"] == 3 and candidate["maximum_temperature_deci_c"] > 0 and
                candidate["sample"]["period_start"] <= candidate["first_rejection_tick"] <
                candidate["sample"]["period_end"], "Rejected seed witness is vacuous")
    start, end = selected["start_simulation_tick"], selected["end_simulation_tick"]
    require(selected["algorithm"] == selected["profile"] == selected["scenario"] == 1 and
            selected["offset"] == 864000 and selected["start_calendar_tick"] == 864000 + start and
            selected["end_calendar_tick"] == 864000 + end and 9600 <= start <= 14400 and
            4800 <= end - start <= 7200 and selected["duration"] == selected["storm_tick_count"] ==
            selected["snow_tick_count"] == end - start and selected["rain_tick_count"] == 0 and
            -800 <= selected["minimum_temperature_deci_c"] <= selected["maximum_temperature_deci_c"] <= 0 and
            0 <= selected["sample_digest"] < 2**64, "All-Snow forced first storm differs")
    boundaries = selected["boundaries"]
    require([row["simulation_tick"] for row in boundaries] == [start - 1, start, end - 1, end],
            "Exact inclusive/exclusive seed boundaries differ")
    for index, row in enumerate(boundaries):
        sample = row["sample"]
        require(row["calendar_tick"] == 864000 + row["simulation_tick"], "Boundary calendar differs")
        if index in (1, 2):
            require(sample["kind"] == 3 and sample["phase"] == 2 and sample["temperature_deci_c"] <= 0 and
                    sample["period_start"] == 864000 + start and sample["period_end"] == 864000 + end,
                    "Snow boundary sample differs")
        else:
            require(sample["kind"] != 3, "Forced storm endpoints are not exclusive")
    world = d["world"]
    require(world["actual_ticks"] == world["canonical_comparisons"] == 24000 and
            world["simulation_minutes"] == 20 and world["consumer_count"] == 10 and
            world["initial_food"] == 1200 and world["final_food"] == 0 and
            world["standard_initial_food"] == 100 and world["camp_capacity"] == 10000 and
            world["ration_interval_ticks"] == 200 and world["ration_checks"] == 120,
            "Actual twenty simulation minute ration horizon differs")
    snapshot(world["initial"], 0, stopped=False)
    initial = world["initial"]["climate"]
    require(initial["snow_cover_permille"] == 650 and initial["snow_cover_remainder"] ==
            initial["wetness_permille"] == initial["wetness_remainder"] == 0 and
            initial["current"]["period_start"] >= 864000, "Origin contains earned history or prior carryover")
    actor = world["actor_control"]
    require(actor["public_stop_calls"] == 1 and actor["accepted_actor_results"] == 10 and
            actor["state_import_mutations"] == 0, "Uninterrupted trace used actor/time/state mutation")
    population(actor["after_stop"], True)
    require(len(world["ration_events"]) == 120, "Physical ration ledger incomplete")
    for index, event in enumerate(world["ration_events"], 1):
        require(event["tick"] == 200 * index and event["consumers"] == 10 and
                event["food_before"] == 1200 - 10 * (index - 1) and event["food_after"] == 1200 - 10 * index,
                "Physical ration arithmetic differs")
        population(event["population"], True)
    require(world["actual_storm_tick_count"] == selected["storm_tick_count"] and
            world["actual_storm_sample_digest"] == selected["sample_digest"] and
            world["minimum_storm_temperature_deci_c"] == selected["minimum_temperature_deci_c"] and
            world["maximum_storm_temperature_deci_c"] == selected["maximum_temperature_deci_c"],
            "Actual World storm disagrees with exhaustive candidate")
    checkpoint_ticks = [start - 1, start, end - 1, end, 24000]
    require([row["simulation_tick"] for row in world["checkpoints"]] == checkpoint_ticks,
            "Real main-trace boundary observations differ")
    for row, tick in zip(world["checkpoints"], checkpoint_ticks, strict=True):
        snapshot(row, tick)
    snapshot(world["final"], 24000)
    require(world["final"] == world["checkpoints"][-1], "Final main trace differs")
    cases = d["persistence"]
    require([case["boundary"] for case in cases] == ["before", "start", "end"], "Three save boundaries differ")
    for case, tick, main in zip(cases, [start - 1, start, end - 1], world["checkpoints"][:3], strict=True):
        require(case["filename"] == f"winter-{case['boundary']}.save" and case["source_tick"] == tick and
                case["source_checksum"] == main["checksum"] and case["source_climate"] == main["climate"] and
                case["source_weather"] == main["weather"] and case["loaded_exact"] is True and
                case["main_trace_not_advanced"] is True and case["continuation_ticks"] == 200 and
                case["pair_world_ticks"] == 400 and case["checksum_same"] is True and len(case["checksums"]) == 200 and
                case["checksums"][-1] == case["final_checksum"] and
                case["final_climate"]["simulation_tick"] == tick + 200, "Actual save/load continuation differs")
        next_tick = case["next_tick"]
        require(next_tick["simulation_tick"] == tick + 1 and next_tick["checksum"] == case["checksums"][0],
                "Loaded first tick differs")
        if case["boundary"] == "before":
            require(next_tick["sample"] == boundaries[1]["sample"], "Loaded start-inclusive sample differs")
        elif case["boundary"] == "end":
            require(next_tick["sample"] == boundaries[3]["sample"], "Loaded end-exclusive sample differs")
        else:
            require(next_tick["sample"]["kind"] == 3 and next_tick["sample"]["phase"] == 2 and
                    next_tick["sample"]["temperature_deci_c"] <= 0, "Loaded next tick is not Snow storm")
    require(len(result["open_delivery"]) == 6 and
            result["measured"]["output_bytes_before_report"] == sum((root / name).stat().st_size for name in SAVES),
            "Output/scope diagnostic differs")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    results, counts = verify_cohort(args.build, args.first, args.second, "winter", "WeatherWinterProbe.exe",
        {"src/sim/world.cpp", "src/sim/weather.cpp", "src/sim/world_save.cpp", "tools/weather_winter_probe.cpp",
         "tools/build_weather_winter_isolated.ps1", "tools/run_weather_winter_native.py",
         "tests/weather_winter_evidence_tests.py", "tests/weather_cpu_evidence.py"},
        SAVES, SAVES | {"weather-winter-report.json"},
        {"search_ms", "world_trace_ms", "total_ms", "output_bytes_before_report"})
    for result, root in zip(results, (args.first, args.second), strict=True):
        checks(result, root.resolve(strict=True))
    print("weather winter evidence checks passed; " + ", ".join(f"{key}={value}" for key, value in counts.items()))


if __name__ == "__main__":
    main()
