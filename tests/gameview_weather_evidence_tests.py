"""Read-only bindings and repeat evidence for the authoritative weather GPU probe."""
from __future__ import annotations

import argparse
import math
import struct
from collections import Counter
from pathlib import Path

from gameview_cumulative_evidence_tests import bound_rows, digest, hash_row, load, require


WORLD_SAVES = {"weather-world-dry.save", "weather-world-wet.save", "weather-world-reset.save"}
CAPTURES = {f"{name}.png" for name in (
    "authority-dry", "explicit-dry", "authority-wet", "explicit-wet", "wet-all-off",
    "wet-both-on", "wet-ground-off", "wet-sprite-off", "after-invalid", "paused-base",
    "paused-repeat", "resumed", "load-reset", "remote-authority")}
RAW_FILES = {"weather-carrier.rgba8", "weather-wet-qualified.mask8"}
NATIVE_OUTPUTS = WORLD_SAVES | CAPTURES | RAW_FILES | {"weather-report.json"}
STRICT_FLAGS = {"/std:c++20", "/O2", "/MT", "/W4", "/WX", "/permissive-", "/utf-8"}
POSITIVE_IDS = {"CPU-projection-table", "CPU-signed-wet", "AUTH-dry-equivalent", "AUTH-wet-equivalent",
    "GPU-ground-wet", "GPU-sprite-wet", "GPU-window-dry", "SOURCE-local-emission-invariant",
    "TIME-local-1-2-4", "TIME-pause-load-reset", "REMOTE-loopback-authority", "CARRIER-geometry-picks-invariant"}
INVALID_IDS = {f"SOURCE-{name}" for name in (
    "invalid-enum", "clock-conflict", "manual-atmosphere", "environment-version", "cloud-nan", "cloud-below",
    "haze-above", "ground-nan", "ground-below", "ground-above", "wind-nan", "wind-below", "wind-above",
    "missing-env", "water-disabled", "time-enum", "wall-nan", "unknown-sprite", "zero-owner", "unready", "reconnecting")}
COMPONENT_INVALID_IDS = {f"CPU-{name}" for name in (
    "unknown", "tick-mismatch", "shown-negative", "shown-nan", "shown-infinity", "shown-horizon")}
OPEN_SCOPE = {"normal startup", "coherent production art", "precipitation FX", "snow materials and FX",
    "footprints", "full suite", "physical two-PC", "reference visuals", "video", "FPS",
    "fine-grained remote interpolation; existing component evidence only",
    "GameView incomplete-recovery transition; getter mask verified separately",
    "contact/shadow validation; existing component evidence only"}
CLOUD = (0., .6, .85, 1.)
HAZE = (.05, .12, .18, .35)


def close(actual: float, expected: float) -> bool:
    return type(actual) in (int, float) and math.isfinite(actual) and abs(actual - expected) <= 1e-7


def require_ids(rows: list[dict], expected: set[str], fields: tuple[str, ...]) -> None:
    require(len(rows) == len(expected) and {row["id"] for row in rows} == expected and
            all(row[field] is True for row in rows for field in fields), "Missing, duplicate or failed native controls")


def verify_projection(row: dict, weather: dict) -> None:
    current = weather["current"]
    kind = current["kind"]
    require(weather["known"] is True and weather["algorithm_version"] == 1 and
            weather["profile"] == 1 and weather["scenario"] == 0 and weather["offset"] == 0 and
            weather["evaluated_tick"] == weather["calendar_tick"] == 7200 and
            type(kind) is int and 0 <= kind < 4, "Authoritative source identity/sample differs")
    require(row["version"] == 1 and row["source_simulation_tick"] == weather["evaluated_tick"] and
            row["source_calendar_tick"] == weather["calendar_tick"] and close(row["shown_calendar_tick"], 7200) and
            all(row[key] == current[key] for key in ("kind", "precipitation", "temperature_deci_c", "wind_direction")),
            "Projection source/semantic fields differ")
    environment = row["environment"]
    require(len(environment) == 5 and environment[0] == 1 and environment[4] is True and
            close(environment[1], (6 + row["shown_calendar_tick"] / 1200) % 24) and
            close(environment[2], CLOUD[kind]) and close(environment[3], HAZE[kind]), "Projected environment differs")
    wetness = min(1., max(0., (weather["wet_permille"] + weather["wet_remainder"] / 28800) / 1000))
    require(close(row["wetness01"], wetness) and close(row["snow_cover01"], weather["snow_permille"] / 1000) and
            close(row["wind01"], current["wind_permille"] / 1000) and
            close(row["precipitation01"], current["precipitation_permille"] / 1000), "Authoritative normalization differs")
    cards = weather["forecast"]
    require(len(cards) == 4 and [card["day"] for card in cards] == [1, 2, 3, 4] and
            all(1 <= card["interval_count"] <= 8 and len(card["intervals"]) == 8 and
                all(interval == [0, 0, 0] for interval in card["intervals"][card["interval_count"]:])
                for card in cards), "Full seedless forecast payload differs")


def verify_semantics(root: Path, kit: Path, native: dict) -> None:
    from PIL import Image

    require(set(native) == {"schema", "complete", "backend", "gpu", "driver", "resolution", "graph",
                            "fps", "gpu_execution_ms", "deterministic", "measured"}, "Native root schema differs")
    require(native["backend"] == "direct3d12" and native["resolution"] == [1920, 1080] and
            native["graph"] == "WaterAtmosphereSlice" and native["fps"] is None and native["gpu_execution_ms"] is None and
            isinstance(native["gpu"], str) and native["gpu"] and isinstance(native["driver"], str) and native["driver"],
            "Native backend or unmeasured performance scope differs")
    value = native["deterministic"]
    require(set(value) == {"controls", "invalid_controls", "clock_controls", "captures", "world_saves",
        "capture_limit", "categories", "defaults_explicit", "fixture", "component", "carrier", "authority",
        "source_invariance", "causal", "remote", "raw_files", "raw_carrier_exact", "opaque_ui_exact", "open"},
        "Deterministic schema differs")
    require(set(value["world_saves"]) == WORLD_SAVES and len(value["world_saves"]) == 3 and
            set(value["raw_files"]) == RAW_FILES and len(value["raw_files"]) == 2 and
            value["capture_limit"] == 20 and value["categories"] == ["house", "resident.empty"] and
            value["defaults_explicit"] is True and value["raw_carrier_exact"] is True and value["opaque_ui_exact"] is True,
            "Declared coverage/defaults differ")
    require(OPEN_SCOPE <= set(value["open"]) and len(value["open"]) == len(set(value["open"])), "Delivery scope is overstated")
    require(value["fixture"] == {"seed": 70421, "ticks": [7200, 7200, 7210], "wet_permille": [0, 600, 600],
        "wet_remainder": [0, 14400, 14400], "actors": 0, "cover_history_replayed": False,
        "geometry": "Authored revealed banks, hidden continuation and three completed Bridge records"},
        "Host-only fixture contract differs")
    require_ids(value["controls"], POSITIVE_IDS, ("passed",))
    require_ids(value["invalid_controls"], INVALID_IDS, ("rejected", "unchanged", "frame_camera_pick_clock_exact"))
    require(all(row["authority_tick"] == 7204 and close(row["recovery_shown_tick"], 7202.2)
                for row in value["invalid_controls"]), "Invalid candidate clock witness differs")
    component = value["component"]
    require(component["scope"] == "Seedless legal structural views; not a generated World schedule" and
            len(component["table"]) == 4, "Component schedule scope differs")
    require_ids(component["invalid_controls"], COMPONENT_INVALID_IDS, ("rejected",))
    for kind, row in enumerate(component["table"]):
        require(row["version"] == 1 and row["source_simulation_tick"] == row["source_calendar_tick"] == 7200 and
                close(row["shown_calendar_tick"], 7200) and row["kind"] == kind and row["wind_direction"] == 1 and
                row["precipitation"] == (2 if kind >= 2 else 0) and
                row["temperature_deci_c"] == (-50 if kind >= 2 else 50) and
                close(row["precipitation01"], .8 if kind >= 2 else 0) and close(row["wind01"], .3) and
                close(row["wetness01"], .6005) and close(row["snow_cover01"], .65) and
                row["environment"][0] == 1 and row["environment"][4] is True and
                close(row["environment"][1], 12) and close(row["environment"][2], CLOUD[kind]) and
                close(row["environment"][3], HAZE[kind]), "Literal projection table differs")
    require(len(component["signed_wet"]) == 2 and all(
        row["permille"] == 600 and row["remainder"] == remainder and close(row["normalized"], expected)
        for row, remainder, expected in zip(component["signed_wet"], (-14400, 14400), (.5995, .6005), strict=True)),
        "Signed wet remainder witness differs")
    authority = value["authority"]
    require(len(authority) == 2 and [row["id"] for row in authority] == ["dry", "wet"], "Authority pair differs")
    for row, wet, remainder in zip(authority, (0, 600), (0, 14400), strict=True):
        require(row["frame_equal"] is True and row["pixels_equal"] is True and
                row["source_weather"]["wet_permille"] == wet and row["source_weather"]["wet_remainder"] == remainder,
                "Authority equivalent frame witness differs")
        verify_projection(row["projection"], row["source_weather"])
        payload = row["payload"]
        require(close(payload["water"][0][2], 360) and close(payload["atmosphere"][0][2], 360) and
                close(payload["water"][0][3], row["projection"]["wind01"]) and payload["counts"][0] == 3,
                "Shared phase, wind or local count differs")
    require(authority[0]["payload"] == authority[1]["payload"] and
            {k: v for k, v in authority[0]["source_weather"].items() if k not in {"wet_permille", "wet_remainder"}} ==
            {k: v for k, v in authority[1]["source_weather"].items() if k not in {"wet_permille", "wet_remainder"}},
            "Wet-only fixture changed other authority/payload semantics")
    require(value["source_invariance"] == {"local_bytes_exact": True, "emission_metadata_exact": True,
        "wet_only_other_fields_exact": True, "incoming_local_rows": 1, "authored_local_rows": 2}, "Source invariance differs")
    remote = value["remote"]
    require(all(remote[key] is True for key in ("received_weather_equal_host", "ready", "world_ready",
        "explicit_frame_equal", "disconnect_masks_weather", "reconnecting_rejected", "source_save_bytes_unchanged",
        "source_save_backups_absent")) and remote["source_tick"] == 7200 and
        remote["source_weather"] == authority[1]["source_weather"] and remote["projection"] == authority[1]["projection"] and
        remote["transport"] == "Actual loopback GNS full snapshot; no client injection" and
        remote["disconnect_scope"] == "Actual host destruction closes GNS transport; explicit NetworkServer shutdown/save not invoked",
        "Received authority scope differs")
    clocks = value["clock_controls"]
    require_ids(clocks, {"speed-1", "speed-2", "speed-4", "pause-load-reset"}, ("passed",))
    for row in clocks:
        if row["id"].startswith("speed-"):
            speed = int(row["id"].split("-")[1])
            require(row["speed"] == speed and row["driver_frames"] == 10 and row["steps"] == 20 * speed and
                    row["authority_tick"] == 7200 + 20 * speed and row["shown_tick"] == 7200 + 19 * speed and
                    row["resume_tick"] == 7200 + 22 * speed and row["pause_exact"] is True, "Actual speed clock differs")
        else:
            require(row["paused_compositions"] == 8 and row["paused_steps"] == 0 and row["paused_pixels_exact"] is True and
                    row["resumed_tick"] == 7290 and row["repeated_epoch"] is True and close(row["lagged_shown_tick"], 7202.2) and
                    row["reset_shown_tick"] == row["reset_source_tick"] == 7210, "Pause/load reset clock differs")
    captures = value["captures"]
    require(len(captures) == 14 and {row["file"] for row in captures} == CAPTURES and
            all(row["bytes"] == (root / row["file"]).stat().st_size and row["water_draws"] > 0 and
                row["reflection_draws"] > 0 and row["graph_passes"] == 8 and row["gpu_execution_ms"] is None
                for row in captures), "Capture coverage/graph differs")
    images = {}
    for name in CAPTURES:
        with Image.open(root / name) as image:
            rgba = image.convert("RGBA")
            require(rgba.size == (1920, 1080), "Decoded capture extent differs")
            pixels = rgba.tobytes()
            require(set(pixels[3::4]) == {255}, "Final scene alpha is not opaque")
            require(rgba.crop((14, 14, 90, 50)).tobytes() == bytes((31, 189, 78, 255)) * (76 * 36),
                    "Opaque UI marker differs")
            images[name] = pixels
    for first, second in (("authority-dry.png", "explicit-dry.png"), ("authority-dry.png", "after-invalid.png"),
                          ("authority-wet.png", "explicit-wet.png"), ("paused-base.png", "paused-repeat.png")):
        require(images[first] == images[second], "Within-run equivalent pixels differ")
    pause_row = next(row for row in clocks if row["id"] == "pause-load-reset")
    resume_changed = sum(images["paused-base.png"][p:p+4] != images["resumed.png"][p:p+4]
                         for p in range(0, 1920 * 1080 * 4, 4))
    require(resume_changed > 0 and resume_changed == pause_row["resumed_changed_pixels"], "Resume PNG witness differs")
    carrier = (root / "weather-carrier.rgba8").read_bytes()
    tuples = Counter(zip(carrier[0::4], carrier[1::4], carrier[2::4], carrier[3::4]))
    require(all(t in {(0, 0, 0, 0), (0, 1, 0, 0), (0, 3, 0, 0)} or
                t[0] == 255 and t[1] == 2 and t[2] % 17 == 0 and t[3] == 255 for t in tuples), "Invalid carrier tuple")
    packed = value["carrier"]
    actual_counts = {"clear_pixels": tuples[(0, 0, 0, 0)], "land_pixels": tuples[(0, 1, 0, 0)],
        "bridge_deck_pixels": tuples[(0, 3, 0, 0)], "water_pixels": sum(n for t, n in tuples.items() if t[1] == 2),
        "shore_water_pixels": sum(n for t, n in tuples.items() if t[1] == 2 and t[2] != 0)}
    require(all(count > 0 and packed[key] == count for key, count in actual_counts.items()) and
            packed["width"] == 1920 and packed["height"] == 1080 and packed["packed_bytes"] == len(carrier) and
            packed["gpu_row_pitch"] >= 7680 and packed["gpu_row_pitch"] % 256 == 0 and packed["invalid_tuples"] == 0 and
            packed["coverage_scope"] == "exact binary carrier; intermediate AA coverage not implemented", "Raw carrier counts differ")
    mask = (root / "weather-wet-qualified.mask8").read_bytes()
    classes = Counter(mask)
    require(set(classes) <= {0, 1, 2, 3} and classes[1] > 1000 and classes[2] >= 32 and classes[3] >= 8,
            "Causal qualification is vacuous")
    causal = value["causal"]
    require(causal["ground_qualified"] == classes[1] and causal["sprite_qualified"] == classes[2] and
            causal["dry_window_qualified"] == classes[3] and causal["dry_window_exact"] is True and
            causal["geometry_camera_picks_exact"] is True and causal["water_wetness_one"] is True and
            causal["uv_regions"] == [0, 0, 1, 1] and close(causal["wetness"], .8) and
            close(causal["ground_dry_roughness"], 1) and close(causal["ground_wet_roughness"], .6) and
            close(causal["ground_dry_specular"], .05) and close(causal["ground_wet_specular"], .09), "Causal material contract differs")
    descriptor = load(kit / "art/world.json")
    texture = next(t for t in descriptor["textures"] if t["id"] == "control.house.texture.albedo")
    require(causal["texture_size"] == [texture["width"], texture["height"]] and
            causal["source_footprint"] == "4x4 texels around bilinear footprint; alpha255, exterior red>=128/emission0, window red0/emission>=250" and
            causal["oracle"] == "Final UNORM8 inverse-sRGB quantization intervals; not raw HDR", "Authored qualification scope differs")
    slope, eye = .65, 1 / math.sqrt(3)
    ny, nz = slope / math.sqrt(1 + slope**2), 1 / math.sqrt(1 + slope**2)
    dot = eye * (ny + nz)
    require(all(close(a, b) for a, b in zip(causal["sun_direction"], (-eye, 2 * dot * ny - eye, 2 * dot * nz - eye, 0), strict=True)),
            "Fixed causal sun direction differs")
    def inverse_srgb(v: float) -> float:
        return v / 12.92 if v <= .04045 else ((v + .055) / 1.055)**2.4
    intervals = [(inverse_srgb(max(0., (byte - .5) / 255)),
                  math.inf if byte == 255 else inverse_srgb((byte + .5) / 255)) for byte in range(256)]
    dry, ground, sprite, both = (images[name] for name in (
        "wet-all-off.png", "wet-sprite-off.png", "wet-ground-off.png", "wet-both-on.png"))
    ground_changed = sprite_changed = bins = nonsaturated = 0
    carrier_tags = carrier[1::4]
    neighbor_offsets = [dy * 1920 + dx for dy in range(-2, 3) for dx in range(-2, 3)]
    for pixel, kind in enumerate(mask):
        if kind == 0:
            continue
        offset = pixel * 4
        if kind == 1:
            require(carrier[offset:offset+4] in (bytes((0, 1, 0, 0)), bytes((0, 3, 0, 0))),
                    "Qualified ground is not land/deck")
            require(2 <= pixel % 1920 < 1918 and 2 <= pixel // 1920 < 1078 and
                    all(carrier_tags[pixel + delta] in (1, 3) for delta in neighbor_offsets),
                    "Qualified ground lacks its 5x5 carrier margin")
            ground_changed += dry[offset:offset+4] != ground[offset:offset+4]
        elif kind == 2:
            sprite_changed += dry[offset:offset+4] != sprite[offset:offset+4]
            for channel in range(3):
                a, b = intervals[dry[offset+channel]], intervals[sprite[offset+channel]]
                bins += a[1] < b[0] or b[1] < a[0]
        else:
            require(dry[offset:offset+4] == ground[offset:offset+4] == sprite[offset:offset+4] == both[offset:offset+4],
                    "Qualified dry window changed")
            nonsaturated += any(byte < 250 for byte in dry[offset:offset+3])
    require(ground_changed == causal["ground_changed"] and ground_changed >= 100 and
            sprite_changed == causal["sprite_changed"] and sprite_changed >= 16 and
            bins == causal["disjoint_linear_bins"] and bins >= 32 and
            nonsaturated == causal["dry_window_nonsaturated"] and nonsaturated >= 8,
            "Actual PNG causal/linear-interval witnesses differ")


def verify_bindings(build: Path, kit: Path, first: Path, second: Path) -> tuple[list[dict], dict]:
    build, kit, first, second = (p.resolve(strict=True) for p in (build, kit, first, second))
    require(first != second and first != build and second != build, "Distinct build and native roots required")
    report = load(build / "build-report.json")
    repository = Path(report["authoritative_repository"])
    require(repository == Path(r"C:\Users\fipst\Desktop\ribterra"), "Authoritative repository differs")
    require(Path(report["output_root"]) == build and Path(report["control_kit"]["root"]) == kit,
            "Build/kit binding mismatch")
    require(report["build_label"] == "gameview-weather-isolated-unversioned" and
            report["translation_unit_count"] == 23 and
            report["internal_production_translation_unit_count"] == 22 and
            len(report["translation_units"]) == 23 and len(set(report["translation_units"])) == 23 and
            "src/sim/weather.cpp" in report["translation_units"], "Fresh weather TU closure differs")
    require(not any(report["execution"].values()) and not any(report["claims"].values()),
            "Builder overstates execution or delivery")
    checked: set[Path] = set()
    bound_rows(report, checked)
    for row in report["runtime_files"]:
        for field in ("origin", "destination"):
            hash_row(row, Path(row[field]))
            checked.add(Path(row[field]))
    sources = report["source_provenance"]
    source_names = {row["relative_path"] for row in sources}
    required_sources = {
        "tools/gameview_weather_probe.cpp", "tools/engine_control_probe.hpp",
        "tools/build_gameview_weather_isolated.ps1", "tools/run_gameview_weather_native.ps1",
        "tools/export_engine_control_kit.py", "tools/export_material_master.py",
        "tests/gameview_weather_evidence_tests.py", "tests/gameview_cumulative_evidence_tests.py",
        "tests/engine_control_kit_tests.py", "include/ribterra/weather_presentation.hpp",
        "include/ribterra/view.hpp", "src/client/view.cpp", "src/sim/weather.cpp",
        "src/sim/world.cpp", "src/sim/world_save.cpp", "src/client/client_session.cpp",
        "src/net/protocol.cpp", "shaders/water_ground_lighting.frag.hlsl",
        "shaders/atmosphere_material_forward.frag.hlsl",
    }
    require(len(source_names) == len(sources) and required_sources <= source_names,
            "Weather source closure missing or duplicate")
    require(source_names == {p.relative_to(build / "source").as_posix()
                             for p in (build / "source").rglob("*") if p.is_file()},
            "Snapshot source inventory differs")
    include_names = {p.relative_to(repository).as_posix()
                     for p in (repository / "include").rglob("*") if p.is_file()}
    require({name for name in source_names if name.startswith("include/")} == include_names,
            "Current whole-include inventory differs from the snapshot")
    for row in sources:
        relative = Path(row["relative_path"])
        require(not relative.is_absolute() and ".." not in relative.parts and
                Path(row["repository_path"]) == repository / relative and
                Path(row["snapshot_path"]) == build / "source" / relative, "Source origin/snapshot linkage differs")
        for field in ("repository_path", "snapshot_path"):
            hash_row(row, Path(row[field]))
            checked.add(Path(row[field]))
    final = report["source_final_binding"]
    require(len(final) == len(sources) and {row["relative_path"] for row in final} == source_names and
            all(row["unchanged"] for row in final), "Source changed during build")
    source_by_name = {row["relative_path"]: row for row in sources}
    require(all({key: value for key, value in row.items() if key != "unchanged"} ==
                source_by_name[row["relative_path"]] for row in final), "Final source bindings differ")
    require(report["protected_binding"] and all(row["unchanged"] for row in report["protected_binding"]),
            "Protected prior evidence changed")
    sidecars = set(build.glob("*.log.exit.txt"))
    logs = {Path(str(path).removesuffix(".exit.txt")) for path in sidecars}
    require(len(sidecars) == 121 and
            all(path.read_text(encoding="utf-8-sig").strip() == "0" for path in sidecars) and
            {Path(row["path"]) for row in report["command_evidence"]} == sidecars | logs,
            "Actual command sidecars/log inventory differs")
    commands = report["commands"]
    require(len(commands) == 121 and len({row["log"]["path"] for row in commands}) == 121 and
            {Path(row["log"]["path"]) for row in commands} == logs and
            {Path(row["exit_sidecar"]["path"]) for row in commands} == sidecars and
            all(row["exit_code"] == 0 and row["arguments"] and
                Path(row["working_directory"]) == Path(report["authoritative_repository"])
                for row in commands), "Actual argv/cwd/exit ledger differs")
    tool_paths = {Path(row["path"]) for row in report["tools"]}
    require(all(Path(row["executable"]) in tool_paths for row in commands), "Command tool is unbound")
    flags = report["flags"]
    require(STRICT_FLAGS <= set(flags), "Strict compiler flags differ")
    compiles = [row for row in commands if Path(row["executable"]).name.lower() == "cl.exe"]
    require(len(compiles) == 23, "Compiler invocation count differs")
    compiled_sources = set()
    for row in compiles:
        argv = row["arguments"]
        require(STRICT_FLAGS <= set(argv) and "/WX-" not in argv and argv.count("/c") == 1,
                "Actual compile lacks strict flags")
        compiled_sources.add(Path(argv[argv.index("/c") + 1]))
    require(compiled_sources == {build / "source" / name for name in report["translation_units"]},
            "Actual compiled sources differ from the bound TU list")
    shader = report["shader_compilation"]
    require(shader["fresh_shader_compilation"] is True and shader["program_count"] == 23 and
            len(shader["programs"]) == 23 and len(set(shader["programs"])) == 23 and
            shader["stage_count"] == 46 and shader["total_file_count"] == 93 and
            len(shader["artifacts"]) == 93, "Fresh shader closure differs")
    shader_names = {Path(row["path"]).name for row in shader["artifacts"]}
    manifest_name = Path(shader["manifest"]["path"]).name
    require(Path(shader["manifest"]["path"]) == build / "assets/shaders/manifest.json" and
            all(Path(row["path"]) == build / "assets/shaders" / Path(row["path"]).name
                for row in shader["artifacts"]), "Fresh shader path linkage differs")
    require(shader_names == {manifest_name} | {
        f"{program}.{stage}.{extension}" for program in shader["programs"]
        for stage in ("vert", "frag") for extension in ("dxil", "json")}, "Shader artifact inventory differs")
    exe = build / "GameViewWaterProbe.exe"
    require(Path(report["executable"]["path"]) == exe, "Executable binding differs")
    require(sum(p.stat().st_size for p in build.rglob("*") if p.is_file()) <= 128 * 1024**2,
            "Build output budget exceeded")
    second_kit = kit.parent / "export-b"
    kit_files = {p.relative_to(kit) for p in kit.rglob("*") if p.is_file()}
    require(second_kit.is_dir() and kit_files == {
        p.relative_to(second_kit) for p in second_kit.rglob("*") if p.is_file()}, "Retained kit inventories differ")
    require({row["relative_path"] for row in report["control_kit"]["files"]} == {
        p.as_posix() for p in kit_files} and not report["control_kit"]["bulk_staged"], "Kit binding inventory differs")
    require(all(Path(row["path"]) == kit / row["relative_path"]
                for row in report["control_kit"]["files"]), "Kit file path linkage differs")
    for relative in kit_files:
        require((kit / relative).read_bytes() == (second_kit / relative).read_bytes(),
                f"Retained kit repeat differs: {relative}")
    results = []
    for root in (first, second):
        execution = load(root / "native-execution.json")
        require(execution["accepted"] is True and execution["actual_subprocess"] is True and
                execution["exit_code"] == 0 and execution["bound_inputs_unchanged"] is True and
                not execution["integrity_errors"], "Actual native execution rejected")
        require(Path(execution["build_root"]) == build and Path(execution["kit_root"]) == kit and
                Path(execution["output_root"]) == root and
                execution["actual_argv"] == [str(exe), str(build / "assets"), str(kit), str(root)],
                "Native roots/actual argv differ")
        require(Path(execution["bound_inputs_before"]["runner"]["path"]) ==
                Path(report["authoritative_repository"]) / "tools/run_gameview_weather_native.ps1",
                "Native weather runner differs")
        bound_rows(execution, checked)
        expected_bindings = {
            "executable": exe, "build_report": build / "build-report.json",
            "fresh_shader_manifest": build / "assets/shaders/manifest.json",
            "kit_manifest": kit / "manifest.json",
            "runner": repository / "tools/run_gameview_weather_native.ps1",
        }
        before = execution["bound_inputs_before"]
        after = execution["bound_inputs_after"]
        require(set(before) == set(after) == set(expected_bindings) and
                all(Path(before[key]["path"]) == path and Path(after[key]["path"]) == path and
                    after[key]["unchanged"] is True and
                    after[key]["bytes"] == after[key]["after_bytes"] == before[key]["bytes"] and
                    after[key]["sha256"].lower() == after[key]["after_sha256"].lower() == before[key]["sha256"].lower()
                    for key, path in expected_bindings.items()), "Native selected input linkage differs")
        for key, row in (("executable", report["executable"]), ("fresh_shader_manifest", shader["manifest"]),
                         ("kit_manifest", next(row for row in report["control_kit"]["files"]
                                               if row["relative_path"] == "manifest.json")),
                         ("runner", source_by_name["tools/run_gameview_weather_native.ps1"])):
            require(before[key]["bytes"] == row["bytes"] and
                    before[key]["sha256"].lower() == row["sha256"].lower(), "Native build/source identity differs")
        recorded = execution["native_output_before_runner_evidence"]["files"]
        names = {row["relative_path"] for row in recorded}
        actual = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}
        require(len(names) == len(recorded) and names == NATIVE_OUTPUTS and
                actual == names | {"native-execution.json", "execution-output.txt"}, "Exact native output inventory differs")
        require(all(Path(row["path"]) == root / row["relative_path"] for row in recorded), "Native output path differs")
        require(sum(p.stat().st_size for p in root.rglob("*") if p.is_file()) <= 128 * 1024**2,
                "Native output budget exceeded")
        native = load(root / "weather-report.json")
        require(native["schema"] == 1 and native["complete"] is True, "Native report incomplete")
        measured = native["measured"]
        require(set(measured) == {"total_ms"} and
                all(type(v) in (float, int) and math.isfinite(v) and v >= 0 for v in measured.values()),
                "Invalid or unbound varying diagnostic")
        for name in CAPTURES:
            header = (root / name).read_bytes()[:24]
            require(len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR" and
                    struct.unpack(">II", header[16:24]) == (1920, 1080), "Native PNG dimensions differ")
        require((root / "weather-carrier.rgba8").stat().st_size == 1920 * 1080 * 4 and
                (root / "weather-wet-qualified.mask8").stat().st_size == 1920 * 1080, "Raw readback dimensions differ")
        results.append(native)
    for name in NATIVE_OUTPUTS - WORLD_SAVES - {"weather-report.json"}:
        require((first / name).read_bytes() == (second / name).read_bytes(), f"Derived output repeat differs: {name}")
    require({key: value for key, value in results[0].items() if key != "measured"} ==
            {key: value for key, value in results[1].items() if key != "measured"},
            "Stable semantic weather report differs")
    hash_row({"bytes": exe.stat().st_size, "sha256": digest(exe)}, exe)
    try:
        hash_row({"bytes": exe.stat().st_size, "sha256": "0" * 64}, exe)
    except AssertionError:
        pass
    else:
        raise AssertionError("Binding negative control failed")
    return results, {"sources": len(sources), "bound_files": len(checked), "zero_commands": 121,
                     "exact_derived_files": len(NATIVE_OUTPUTS - WORLD_SAVES - {"weather-report.json"})}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "kit", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    results, counts = verify_bindings(args.build, args.kit, args.first, args.second)
    for root, native in zip((args.first, args.second), results, strict=True):
        verify_semantics(root, args.kit, native)
    print("weather GameView evidence checks passed; " + ", ".join(f"{key}={value}" for key, value in counts.items()))


if __name__ == "__main__":
    main()
