"""Actual typed-alpha build/native/repeat outcomes; inferred SDR scope only."""
from __future__ import annotations

import argparse
from pathlib import Path

from gameview_cumulative_evidence_tests import require, verify_current_cohort

API_REJECTIONS = {"opacity_negative", "opacity_above_one", "opacity_nan", "opacity_pos_inf",
                  "opacity_neg_inf", "zero_unknown_asset", "zero_invalid_zoom", "zero_invalid_emission",
                  "zero_non_hdr", "zero_anchor_nan"}
RENDERER_REJECTIONS = {"sprite_rgb_red", "sprite_rgb_green", "sprite_rgb_blue", "reflection_rgb_red",
    "reflection_rgb_green", "reflection_rgb_blue", "sprite_emission_nan", "sprite_anchor_nan",
    "sprite_reserved_response", "sprite_reveal_fractional", "sprite_receiver_zero",
    "reflection_strength_nan", "reflection_fade_below_one", "reflection_uv_out_of_bounds", "ground_alpha_nonopaque"}
CPU_CONTROLS = {"default_explicit_one_frame_exact", "zero_and_quantized_zero_no_appends",
                "white_rgb_quantized_alpha_128_129_255"}


def require_ids(cases: list[dict], expected: set[str]) -> None:
    require(len(cases) == len(expected) and {case["id"] for case in cases} == expected and
            all(case["passed"] for case in cases), "Missing, duplicate or failed opacity controls")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "kit", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    required_sources = {"tools/gameview_opacity_probe.cpp", "tools/engine_control_probe.hpp",
        "include/ribterra/game_art.hpp", "src/client/game_art.cpp", "src/render/renderer2d.cpp",
        "shaders/material_sprite.frag.hlsl", "shaders/water_reflection.frag.hlsl",
        "tests/gameview_cumulative_evidence_tests.py", "tests/gameview_opacity_evidence_tests.py"}
    results, counts = verify_current_cohort(args.build, args.kit, args.first, args.second,
        required_sources, "opacity-native-result.json", {"opacity-world-current.save"})
    for root, native in zip((args.first, args.second), results, strict=True):
        require(native["categories"] == ["house"] and native["world_saves"] == ["opacity-world-current.save"],
                "Unexpected category/save coverage")
        require(native["backend"] == "direct3d12" and native["resolution"] == [1920, 1080] and
                native["graph"] == "WaterAtmosphereSlice", "Native backend/graph differs")
        require(len(native["captures"]) == 19 and native["capture_limit"] == 20 and
                all(capture["graph_passes"] == 8 and capture["water_draws"] > 0 and
                    capture["gpu_execution_ms"] is None for capture in native["captures"]), "Capture coverage differs")
        require(native["raw_carrier_exact"] and native["opaque_ui_exact"] and native["repeat_exact"] and
                native["known_ui_rgba"] == [31, 189, 78, 255], "Carrier/UI/repeat control failed")
        require(native["raw_hdr_readback"] is False and native["inferred_sdr_intervals"] is True and
                native["new_bindings"] is False and native["weather_explicit"] is True and
                native["remote_verified"] is False and native["normal_startup"] == "OPEN", "Overstated delivery scope")
        require_ids(native["cpu_controls"], CPU_CONTROLS)
        require_ids(native["api_rejections"], API_REJECTIONS)
        require_ids(native["renderer_rejections"], RENDERER_REJECTIONS)
        for case in native["api_rejections"] + native["renderer_rejections"]:
            require(case["frame_exact"] and case["submitted"] is False, "Invalid candidate mutated/submitted")
        require(all(case["target_creation_unchanged"] for case in native["renderer_rejections"]),
                "Invalid renderer candidate allocated targets")
        require_ids(native["groups"], {"coverage", "emission", "reflection"})
        carrier = (root / "opacity-carrier.rgba8").read_bytes()
        require(len(carrier) == 1920 * 1080 * 4, "Packed carrier dimensions differ")
        for group in native["groups"]:
            require(all(group[key] for key in ("zero_api_frame_exact", "zero_api_png_equal", "zero_geometry_png_equal",
                "default_one_frame_equal", "default_one_png_equal", "metadata_except_alpha_exact",
                "zero_endpoint_substitution_rejected", "one_endpoint_substitution_rejected")), "Group equality/oracle failed")
            require(group["half_alpha_byte"] == 128 and group["one_alpha_byte"] == 255 and
                    group["fixed_exposure"] == 1 and group["fog_enabled"] is False and
                    group["half_changed_from_zero"] > 16 and group["half_changed_from_one"] > 16,
                    "Group alpha/causality differs")
            oracle = group["interval_oracle"]
            require(oracle["intervals_passed"] and oracle["alpha_numerator"] == 128 and oracle["alpha_denominator"] == 255 and
                    oracle["qualified_pixels"] >= 16 and oracle["qualified_components"] >= 32 and
                    oracle["strictly_intermediate_components"] >= 16 and oracle["minimum_endpoint_byte_delta"] == 8 and
                    oracle["fp16_relative_allowance"] == 3 / 1024 and oracle["fp16_absolute_allowance"] == 1 / 65536 and
                    oracle["max_interval_gap"] <= oracle["max_fp16_allowance"], "Inferred SDR oracle differs")
            mask = (root / oracle["mask"]).read_bytes()
            require(len(mask) == 1920 * 1080 and set(mask) <= {0, 1} and sum(mask) == oracle["qualified_pixels"],
                    "Retained qualification mask differs")
            if group["id"] == "reflection":
                require(group["reflection_only"] and all(carrier[p * 4] == 255 and carrier[p * 4 + 1] == 2
                    for p, qualified in enumerate(mask) if qualified), "Reflection mask includes non-water receivers")
    print("opacity current evidence checks passed; " + ", ".join(f"{key}={value}" for key, value in counts.items()))


if __name__ == "__main__":
    main()
