"""Read-only bindings for malformed weather packets and actual localhost recovery."""
from __future__ import annotations

import argparse
from pathlib import Path
from gameview_cumulative_evidence_tests import require
from weather_cpu_evidence import verify_cohort


WIRE = set("""missing_weather missing_current missing_forecast missing_forecast_card
forecast_count_0 forecast_count_3 forecast_count_5 interval_count_0 interval_count_9
unknown_weather algorithm_0 algorithm_2 profile_0 profile_2 scenario_2 kind_enum
precipitation_enum wind_enum work_reason_enum forecast_kind_enum forecast_precipitation_enum
interval_kind_enum evaluated_tick_mismatch calendar_tick_mismatch offset_mismatch
snapshot_day_mismatch snapshot_winter_mismatch forecast_day_mismatch current_period_empty
current_period_not_covering temperature_below temperature_above precipitation_range wind_range
work_rate_below work_rate_above work_reason_mismatch precipitation_kind_mismatch snow_range
wetness_range wet_remainder_positive_bound wet_remainder_negative_bound wet_remainder_outward
forecast_minimum_range forecast_maximum_range forecast_minimum_above_maximum interval_gap
interval_empty interval_end_range interval_order adjacent_identical_intervals forecast_summary_mismatch
forecast_storm_mismatch forecast_dominant_mismatch storm_pair_omitted storm_pair_empty
storm_pair_wrong_start storm_pair_horizon""".split())
APPLY = set("""unknown_weather evaluated_tick_mismatch calendar_tick_mismatch day_mismatch
winter_mismatch snow_range wetness_range wet_remainder_range inactive_interval_nondefault
delta_climate_identity delta_wrong_world delta_baseline_mismatch""".split())
POSITIVE_WIRE = set("""valid_unchanged_object_frame valid_cover_lower_bounds valid_cover_upper_bounds
valid_signed_remainder_positive valid_signed_remainder_negative valid_interval_count_1 valid_interval_count_8""".split())
POSITIVE = POSITIVE_WIRE | set("""legal_identity_delta_decodes sender_identity_delta_rejected
full_identity_replacement identical_part_repeat_pending complete_baseline_after_timeout_reset""".split())
ASSEMBLY = {"malformed_part", "legal_header_weather_mismatch", "conflicting_repeat_metadata", "accept_after_timeout"}
SESSION = {
    "pending_and_waiting_lobby_unknown": (28798, 31),
    "normal_advance_calendar_rollover": (28802, 32),
    "two_client_sessions_late_join": (28802, 32),
    "third_participant_received_world_id": (28802, 32),
    "speed_1": (28806, 32), "speed_2": (28814, 32), "speed_4": (28830, 32),
    "paused_host_ticks_do_not_advance": (28830, 32), "resume_speed_4": (28846, 32),
    "actual_disconnect_auto_resume_identity_and_weather": (28846, 32),
    "actual_incomplete_recovery_masks_weather": (28846, 32),
    "explicit_command_clears_incomplete_recovery": (28846, 32),
}
SAVES = {"transport-boundary.save", "transport-boundary.save.bak"}
PACKETS = {f"wire/{id}.rbnf" for id in WIRE | POSITIVE_WIRE}


def exact_ids(rows: list[dict], expected: set[str]) -> None:
    require(len(rows) == len(expected) and {row["id"] for row in rows} == expected,
            "Exact transport control coverage differs")


def checks(result: dict, root: Path) -> None:
    require(result["schema"] == 1 and result["versions"] ==
            {"protocol": 23, "rules": 19, "generator": 8, "save": 9}, "Version contract differs")
    d = result["deterministic"]
    exact_ids(d["wire_rejections"], WIRE)
    require(d["wire_rejection_count"] == len(WIRE) == 58, "Wire count differs")
    for row in d["wire_rejections"]:
        require(row["decoder_rejected"] is True and row["view_world_weather_exact"] is True and row["error"],
                "Wire rejection/publication control failed")
        require(row["inner_flatbuffer_valid"] is
                (row["id"] not in {"missing_weather", "missing_current", "missing_forecast", "missing_forecast_card"}),
                "Structural/semantic rejection witness differs")
    exact_ids(d["apply_rejections"], APPLY)
    require(d["apply_rejection_count"] == len(APPLY) == 12 and
            all(row["view_world_weather_exact"] is True and row["error"] for row in d["apply_rejections"]),
            "Apply transaction rejection differs")
    exact_ids(d["positive_controls"], POSITIVE)
    require(len(POSITIVE) == 12 and all(row["passed"] is True for row in d["positive_controls"]),
            "Positive transport control failed")
    exact_ids(d["assembly_controls"], ASSEMBLY)
    require(d["multipart_count"] > 1 and
            all(row["passed"] is True and row["view_world_weather_exact"] is True and row["error"]
                for row in d["assembly_controls"]), "Multipart transaction control failed")
    timeout = next(row for row in d["assembly_controls"] if row["id"] == "accept_after_timeout")
    require(timeout["error"] == "Snapshot assembly timed out", "Post-timeout actual accept failed for another reason")
    session = d["session"]
    exact_ids(session["controls"], set(SESSION))
    for row in session["controls"]:
        require(row["passed"] is True and (row["host_tick"], row["calendar_day"]) == SESSION[row["id"]],
                "Actual host ticks/calendar/session control differs")
    require(session["final_host_tick"] == 28846 and session["world_id_equal"] is True and
            session["resume_tokens_equal"] is True and session["client_generation"] is False and
            session["world_saves"] == ["transport-boundary.save", "transport-boundary.save.bak"] and
            session["participants"] == "two ClientSessions and one ordinary audit NetworkClient",
            "Actual reconnect identity/authority scope differs")
    require(len(d["scopes"]) == 5 and len(d["open_delivery"]) == 7 and result["measured"] == {},
            "Checkpoint scope/uncontrolled diagnostics differ")
    for name in PACKETS:
        data = (root / name).read_bytes()
        require(8 < len(data) <= 256 * 1024 and data[4:8] == b"RBNF",
                "Actual rebuilt FlatBuffers outer frame missing or oversized")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    results, counts = verify_cohort(args.build, args.first, args.second, "transport", "WeatherTransportProbe.exe",
        {"src/sim/world.cpp", "src/sim/weather.cpp", "src/sim/world_save.cpp", "src/net/protocol.cpp",
         "src/net/network_session.cpp", "src/client/client_session.cpp", "schemas/network.fbs",
         "tools/weather_transport_probe.cpp", "tools/build_weather_transport_isolated.ps1",
         "tools/run_weather_transport_native.py", "tests/weather_transport_evidence_tests.py", "tests/weather_cpu_evidence.py"},
        SAVES, SAVES | PACKETS | {"weather-transport-report.json"}, set())
    for result, root in zip(results, (args.first, args.second), strict=True):
        checks(result, root.resolve(strict=True))
    print("weather transport evidence checks passed; " + ", ".join(f"{key}={value}" for key, value in counts.items()))


if __name__ == "__main__":
    main()
