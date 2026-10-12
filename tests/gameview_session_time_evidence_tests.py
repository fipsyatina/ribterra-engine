"""Read-only current bindings and native session-time outcome checks."""
from __future__ import annotations

import argparse
from pathlib import Path

from gameview_cumulative_evidence_tests import require, verify_current_cohort

WORLD_SAVES = {"session-time-world-06.save", "session-time-world-12.save",
               "session-time-world-18.save", "session-time-world-00.save",
               "session-time-world-epoch.save"}
CLOCK_CONTROLS = {"authority_lag_interpolation_catchup_starvation",
                  "main_policy_speed_1", "main_policy_speed_2", "main_policy_speed_4",
                  "pause_resume_main_policy", "production_load_reset_repeated_epoch",
                  "pointer_wall_and_authority_rollback", "actual_changed_epoch"}
REJECTIONS = {"enum", "missing_environment", "manual_atmosphere_conflict", "water_disabled",
              "environment_version", "cloud_below_zero", "cloud_above_one", "cloud_nan",
              "haze_below_zero", "haze_above_one", "haze_nan", "camera_nan", "camera_window",
              "unknown_sprite", "zero_owner", "duplicate_occluder", "instance_nan", "wall_nan"}


def require_case_ids(cases: list[dict], expected: set[str], label: str) -> None:
    require(len(cases) == len(expected) and {case["id"] for case in cases} == expected,
            f"Missing, duplicate or unexpected {label} cases")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "kit", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    required_sources = {"tools/gameview_session_time_probe.cpp", "tools/engine_control_probe.hpp",
                        "include/ribterra/view.hpp", "src/client/view.cpp",
                        "include/ribterra/environment_lighting.hpp",
                        "include/ribterra/game_art.hpp", "src/client/game_art.cpp",
                        "tools/export_engine_control_kit.py", "tools/export_material_master.py",
                        "tests/gameview_cumulative_evidence_tests.py",
                        "tests/gameview_session_time_evidence_tests.py"}
    results, counts = verify_current_cohort(args.build, args.kit, args.first, args.second,
        required_sources, "session-time-native-result.json", WORLD_SAVES)
    for native in results:
        require(native["time_source"] == "SessionShown" and native["explicit_weather"] and
                native["remote_verified"] is False, "Time/weather authority scope differs")
        require(set(native["world_saves"]) == WORLD_SAVES, "Unexpected save identity exceptions")
        require(native["backend"] == "direct3d12" and native["resolution"] == [1920, 1080] and
                native["graph"] == "WaterAtmosphereSlice", "Native backend/graph differs")
        require(len(native["captures"]) == 17 and native["capture_limit"] == 20 and
                all(capture["graph_passes"] == 8 and capture["water_draws"] > 0 and
                    capture["reflection_draws"] > 0 and capture["gpu_execution_ms"] is None
                    for capture in native["captures"]), "Capture bound or graph coverage differs")
        require(len(native["calendar_cases"]) == 4 and
                {case["hour"] for case in native["calendar_cases"]} == {0, 6, 12, 18} and
                all(case["payload_equal"] and case["png_equal"] for case in native["calendar_cases"]),
                "Session calendar differs from explicit diagnostic controls")
        require_case_ids(native["clock_controls"], CLOCK_CONTROLS, "clock control")
        require(all(case["passed"] for case in native["clock_controls"]),
                "Native clock controls did not pass")
        require_case_ids(native["transactional_rejections"], REJECTIONS, "transaction rejection")
        require(
                all(case["frame_camera_pick_clock_exact"] for case in native["transactional_rejections"]),
                "Rejected candidates changed published state or shown clock")
        require(native["raw_carrier_exact"] and native["opaque_ui_exact"] and
                native["known_ui_rgba"] == [31, 189, 78, 255] and
                native["locals_emission_metadata_exact"] and native["explicit_diagnostic_default"],
                "Time adapter changed carrier, UI, local lights or emission")
    print("session time current evidence checks passed; " + ", ".join(f"{key}={value}" for key, value in counts.items()))


if __name__ == "__main__":
    main()
