"""Read-only semantic oracles for the bounded normal GameView material probe.

PNG comparisons describe the complete Legacy/HDR routes, not isolated material
lighting causality. Weather rows expose only the current sample: this verifies
calendar/projection consistency, not an independent seeded forecast replay.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
import struct
from collections import Counter
from pathlib import Path

from gameview_cumulative_evidence_tests import digest, load, require

CAPTURE_IDS = (
    "material-menu-first", "material-legacy-default", "material-mixed-default",
    "material-legacy-min", "material-mixed-min", "material-legacy-max", "material-mixed-max",
    "material-legacy-night", "material-mixed-night", "material-mixed-wet", "material-world-hud",
    "material-world-selected", "material-menu-interleave", "material-world-restored", "material-variants",
    "material-winter", "material-animation-fallback", "material-mine-fallback", "material-mine-legacy",
    "material-authority-unavailable",
)
SAVE_NAMES = [f"material-{name}.save" for name in ("day", "night", "wet", "variants", "winter")]
KEYS = ("building.house.ready", "resident.man.walk.2.1")
OPEN_DELIVERY = [
    "Full E01-E17/P01 engine acceptance", "full normal main startup and package/release",
    "production neutral albedo and all eight authored material masters", "reference fidelity",
    "new house draft export/native/production acceptance", "Cumulative graph and independent Moon/weather/water FX",
    "typed geometry outline equivalence", "all resident directions/phases/cargo assets",
    "remote reconnect/recovery_incomplete normal-view route", "physical two-PC/co-op",
    "FPS and GPU execution timing", "full suite on this current cohort",
]
SCOPE = ("Actual normal GameView::compose LegacyCombined diagnostic painted subset; "
         "two-plane authored proxy fixtures, not production/reference-neutral art")
REBINDINGS = ("source_hash/material.source_hash when registration changes; texture hash when pixels change; "
              "manifest bytes/hash/content_sha256")
NEGATIVES = {
    "schema": ("world.schema", "Normal material collection schema/counts are inconsistent"),
    "reference_width": ("items[1].registration.reference_width",
                        "Registered source crop/pivot/reference width/catalog hash differs from original"),
    "pivot": ("items[0].registration.pivot_px[0]",
              "Registered source crop/pivot/reference width/catalog hash differs from original"),
    "decoded_crop_hash": ("items[0].registration.decoded_crop_sha256",
                          "Registered decoded paint hash differs from original crop"),
    "legacy_key": ("items[0].legacy_key",
                   "Normal material mapping requires a unique exact supported original legacy key"),
    "albedo_byte": ("textures[0].payload_byte[0]",
                    "Registered albedo must preserve the exact original cropped RGBA"),
}


def canonical(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def source_hash(value: object) -> str:
    return hashlib.sha256(canonical(value) + b"\n").hexdigest()


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def close_values(actual: list, expected: list, tolerance: float = .0005) -> None:
    require(len(actual) == len(expected) and all(type(a) in (int, float) and math.isfinite(a) and
            abs(a - e) <= tolerance for a, e in zip(actual, expected, strict=True)),
            f"Source-derived values differ: {actual} / {expected}")


def valid_sha(value: object) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def manifest_content(manifest: dict) -> str:
    fields = ("id", "path", "kind", "sha256", "license", "source", "author", "notice")
    rows = manifest["assets"]
    require(len({r["id"] for r in rows}) == len(rows) and len({r["path"] for r in rows}) == len(rows),
            "Ambiguous asset registry")
    payload = bytearray()
    for row in sorted(rows, key=lambda r: r["id"]):
        for field in fields:
            value = row[field].encode("utf-8")
            payload += str(len(value)).encode("ascii") + b":" + value
        payload += b"\n"
    result = hashlib.sha256(payload).hexdigest()
    require(manifest["schema"] == 1 and manifest["content_sha256"] == result, "Registry canonical digest differs")
    return result


def original_sprite(original: Path, world: dict, key: str) -> tuple[dict, dict, tuple[int, int], bytes]:
    from PIL import Image
    found = [(p, s) for p in world["pages"] for s in p["sprites"] if s["id"] == key]
    require(len(found) == 1, f"Ambiguous or absent original key: {key}")
    page, sprite = found[0]
    x, y, w, h = sprite["rect"]
    with Image.open(original / page["path"]) as image:
        size = image.size
        require(0 <= x < x + w <= size[0] and 0 <= y < y + h <= size[1], "Original crop escapes page")
        rgba = image.convert("RGBA").crop((x, y, x + w, y + h)).tobytes()
    return page, sprite, size, rgba


def projected_ground(position: list, zoom: float, foot_offset: float) -> list[float]:
    # world_projection.hpp preserves this historical float grouping. Fixture
    # terrain is flat and camera remains at the original camp (24,24).
    x, y = position
    tw, th = f32(58. * zoom), f32(29. * zoom)
    dx, dy = f32(f32(f32(x - y) - 24.) + 24.), f32(f32(f32(x + y) - 24.) - 24.)
    px = f32(f32(1920. * f32(.48)) + f32(f32(dx * tw) * .5))
    py = f32(f32(1080. * f32(.48)) + f32(f32(dy * th) * .5))
    return [px, f32(py + f32(foot_offset * zoom))]


def verify_legacy_quad(original: Path, world: dict, row: dict, width: float, zoom: float,
                       position: list, foot_offset: float) -> None:
    page, sprite, size, _ = original_sprite(original, world, row["key"])
    ground = projected_ground(position, zoom, foot_offset)
    close_values(row["ground"], ground)
    requested = f32(width * zoom)
    if "requested_width" in row:
        close_values([row["requested_width"]], [requested], .0001)
    rx, ry, sw, sh = sprite["rect"]
    pivot = [f32(v) for v in sprite["pivot"]]
    w = f32(requested * f32(sw / f32(sprite.get("reference_width", sw))))
    h = f32(w * f32(sh / sw))
    bounds = [f32(ground[0] - f32(w * pivot[0])), f32(ground[1] - f32(h * pivot[1])), w, h]
    close_values(row["bounds"], bounds)
    x, y = bounds[:2]
    corners = [[x, y], [f32(x + w), y], [f32(x + w), f32(y + h)], [x, f32(y + h)]]
    u, v, du, dv = f32(rx / size[0]), f32(ry / size[1]), f32(sw / size[0]), f32(sh / size[1])
    uv = [[u, v], [f32(u + du), v], [f32(u + du), f32(v + dv)], [u, f32(v + dv)]]
    quad = row["quad"]
    require(quad["material_index"] == 0 and "surface" not in quad and len(quad["vertices"]) == 4,
            "Unmapped animation/variant used a material or lacks its actual original quad")
    draw = quad["draw"]
    require(len(draw) == 14 and draw[:2] == [1, 1] and draw[2] > 0 and draw[3] > 0 and
            draw[11] is False and draw[12:] == [0, 0] and
            draw[4] <= quad["first_index"] <= draw[4] + draw[5] - 6,
            "Original body crossed a draw/material role")
    for vertex, corner, texcoord in zip(quad["vertices"], corners, uv, strict=True):
        require(len(vertex) == 8 and all(type(c) is int and 0 <= c <= 255 for c in vertex[4:]),
                "Malformed original vertex colour")
        close_values(vertex[:2], corner)
        close_values(vertex[2:4], texcoord, .000001)


def verify_registrations(original: Path, material: Path, world: dict, data: dict, metadata: dict, art: dict) -> None:
    require([r["legacy_key"] for r in data["registrations"]] == list(KEYS) and
            len(metadata["items"]) == len(art["materials"]) == len(art["lit_sprites"]) == 2 and
            art["schema"] == 2 and art["pages"] == [], "Exact diagnostic registered subset differs")
    for row, item, descriptor, lit in zip(data["registrations"], metadata["items"], art["materials"],
                                         art["lit_sprites"], strict=True):
        page, sprite, _, rgba = original_sprite(original, world, item["legacy_key"])
        registration = item["registration"]
        expected_source = source_hash({k: item[k] for k in ("master_sha256", "registration", "geometry_provenance")})
        expected_catalog = source_hash({"page": {"path": page["path"]}, "sprite": sprite})
        sw, sh = sprite["rect"][2:]
        pivot = [f32(p) for p in sprite["pivot"]]
        require(item["sprite_id"] == "dynamic." + item["legacy_key"] == lit["id"] and
                item["material_id"] == descriptor["id"] == lit["material"] and
                item["source_hash"] == descriptor["source_hash"] == expected_source and
                row == {"legacy_key": item["legacy_key"], "sprite_id": item["sprite_id"],
                        "material_id": item["material_id"], "source_hash": expected_source,
                        "source_record_sha256": expected_source, "catalog_entry_sha256": expected_catalog},
                "Accepted registration is not source/catalog bound")
        require(registration["catalog_entry_sha256"] == expected_catalog and
                registration["page_path"] == page["path"] and registration["rect"] == sprite["rect"] and
                registration["source_size"] == [sw, sh] and registration["pivot"] == sprite["pivot"] and
                registration["reference_width"] == f32(sprite.get("reference_width", sw)) and
                registration["png_sha256"] == digest(original / page["path"]) and
                registration["decoded_crop_sha256"] == hashlib.sha256(rgba).hexdigest(),
                "Paint registration differs from original input")
        close_values(registration["pivot_px"], [f32(pivot[0] * sw), f32(pivot[1] * sh)], .000001)
        texture_id = descriptor["channels"]["albedo"]["texture"]
        textures = [t for t in art["textures"] if t["id"] == texture_id]
        require(len(textures) == 1 and (material / textures[0]["path"]).read_bytes() == rgba,
                "Accepted registered paint differs from decoded original crop")


def verify_negatives(root: Path, material: Path, metadata: dict, art: dict, rows: list[dict]) -> None:
    require([r["id"] for r in rows] == list(NEGATIVES), "Missing/duplicate or reordered isolated negatives")
    paths = {p.relative_to(material).as_posix() for p in material.rglob("*") if p.is_file()}
    require(len(paths) == 18, "Bound collection fixture has unexpected closure")
    original_manifest = load(material / "manifest.json")
    for row in rows:
        identity = row["id"]
        fixture = root / ("invalid-" + identity)
        require(row["root"] == fixture.name and row["primary_mutation"] == NEGATIVES[identity][0] and
                row["derived_rebindings"] == REBINDINGS and
                row["error"] == row["expected_error"] == NEGATIVES[identity][1], "Negative rejected for an unintended reason")
        require({p.relative_to(fixture).as_posix() for p in fixture.rglob("*") if p.is_file()} == paths and
                len(row["files"]) == 18 and {r["path"] for r in row["files"]} == paths,
                "Retained negative fixture inventory differs")
        for file in row["files"]:
            path = fixture / file["path"]
            require(file["bytes"] == path.stat().st_size and file["sha256"] == digest(path), "Negative file ledger differs")
        expected_metadata, expected_art = copy.deepcopy(metadata), copy.deepcopy(art)
        changed_path, changed_payload = "", None
        index = None
        if identity == "schema":
            expected_art["schema"] = 3
        elif identity == "reference_width":
            index = 1
            expected_metadata["items"][index]["registration"]["reference_width"] = 139.0
        elif identity == "pivot":
            index = 0
            expected_metadata["items"][index]["registration"]["pivot_px"][0] = 265.5
        elif identity == "decoded_crop_hash":
            index = 0
            expected_metadata["items"][index]["registration"]["decoded_crop_sha256"] = "0" * 64
        elif identity == "legacy_key":
            expected_metadata["items"][0]["legacy_key"] = "building.house.ready.unknown"
        else:
            changed_path = expected_art["textures"][0]["path"]
            payload = bytearray((material / changed_path).read_bytes())
            payload[0] ^= 1
            changed_payload = bytes(payload)
            expected_art["textures"][0]["sha256"] = hashlib.sha256(changed_payload).hexdigest()
        if index is not None:
            item = expected_metadata["items"][index]
            item["source_hash"] = source_hash({k: item[k] for k in ("master_sha256", "registration", "geometry_provenance")})
            expected_art["materials"][index]["source_hash"] = item["source_hash"]
        require(load(fixture / "art/material-collection.json") == expected_metadata and
                load(fixture / "art/world.json") == expected_art, "Negative contains an undeclared semantic mutation")
        for name in paths - {"manifest.json", "art/material-collection.json", "art/world.json"}:
            expected_bytes = changed_payload if name == changed_path else (material / name).read_bytes()
            require((fixture / name).read_bytes() == expected_bytes, "Negative contains an undeclared payload mutation")
        expected_manifest = copy.deepcopy(original_manifest)
        expected_manifest["assets"].sort(key=lambda r: r["id"])
        for asset in expected_manifest["assets"]:
            path = fixture / asset["path"]
            asset["bytes"], asset["sha256"] = path.stat().st_size, digest(path)
        actual_manifest = load(fixture / "manifest.json")
        expected_manifest["content_sha256"] = actual_manifest["content_sha256"]
        require(actual_manifest == expected_manifest, "Negative changed registry identity/provenance")
        manifest_content(actual_manifest)
        require(valid_sha(row["before_frame_sha256"]) and row["before_frame_sha256"] == row["after_frame_sha256"] and
                row["registered_count"] == 2 and row["selected_buildings_before"] == row["selected_buildings_after"] == [2] and
                row["selected_residents_before"] == row["selected_residents_after"] == [] and
                row["camera_before"] == row["camera_after"], "Rejected candidate changed published state")
        close_values(row["camera_before"], projected_ground([24., 24.], f32(1.3), 0.))
    require(len({r["before_frame_sha256"] for r in rows}) == 1, "Isolated negatives did not reuse one unchanged selected frame")


def lighting(hour: float, kind: int) -> tuple[list, list, list]:
    # Independent equations from environment_lighting.hpp. Final float/trig
    # comparisons allow a small arithmetic tolerance; no PNG half-RGB claim.
    cloud = (0., .6, .85, 1.)[kind]
    height = math.sin(math.pi * (hour - 6.) / 12.)
    height = 0. if abs(height) < 1e-6 else max(0., height)
    smooth = lambda a, b, x: (lambda t: t * t * (3. - 2. * t))(min(1., max(0., (x - a) / (b - a))))
    azimuth = math.radians(45. + 180. * (hour - 6.) / 12.)
    elevation = math.radians(20. + 45. * height)
    direction = [math.cos(elevation) * math.cos(azimuth), math.cos(elevation) * math.sin(azimuth),
                 math.sin(elevation), 0.]
    mix = smooth(0., .5, height)
    color = [a + (b - a) * mix for a, b in zip((.95, .55, .30), (1., .95, .82), strict=True)]
    color += [.9 * smooth(0., .2, height) * (1. - .75 * cloud)]
    daylight = smooth(5., 9., hour) * (1. - smooth(16., 21., hour))
    ambient = [a + (b * (1. - .45 * cloud) - a) * daylight
               for a, b in zip((.025, .04, .07), (.22, .24, .25), strict=True)] + [0.]
    return ambient, direction, color


def visible_world(raw: bytes) -> tuple[int, int, int]:
    require(len(raw) == 1920 * 1080 * 4, "World pixel extent differs")
    nonblack = sum(any(raw[index:index + 3]) for index in range(0, len(raw), 4))
    colors = Counter(zip(raw[0::4], raw[1::4], raw[2::4], raw[3::4]))
    unique = len(colors)
    nonmodal = 1920 * 1080 - max(colors.values())
    require(nonblack > 10000 and unique > 64 and nonmodal > 10000, "Standalone World pixels are vacuous")
    return nonblack, unique, nonmodal


def verify_semantics(root: Path, original: Path, material: Path, native: dict,
                     verify_frame_contract, verify_query) -> dict:
    """Read retained inputs only; callbacks own the shared frame/query oracles."""
    from PIL import Image, ImageChops
    require(set(native) == {"schema", "complete", "deterministic", "measured"} and
            native["schema"] == 1 and native["complete"] is True, "Native semantic report incomplete")
    require(set(native["measured"]) == {"total_ms"} and type(native["measured"]["total_ms"]) in (int, float) and
            math.isfinite(native["measured"]["total_ms"]) and native["measured"]["total_ms"] >= 0,
            "Diagnostic elapsed time is invalid")
    data = native["deterministic"]
    require(set(data) == {"fixtures", "captures", "zoom_cases", "variants", "animation_cases", "authority_cases",
            "world_saves", "capture_limit", "hardware", "inputs", "registrations", "cold_load_rejections",
            "cold_install", "ui", "post_submit_rejection", "saved_worlds_preserved", "open_delivery", "scope"},
            "Native semantic section inventory differs")
    require(data["world_saves"] == SAVE_NAMES and data["capture_limit"] == 20 and data["saved_worlds_preserved"] is True and
            data["open_delivery"] == OPEN_DELIVERY and data["scope"] == SCOPE,
            "Authored diagnostic scope or saved-world controls differ")
    require({p.name for p in root.rglob("*.save")} == set(SAVE_NAMES) and
            {p.name for p in root.rglob("*.png")} == {name + ".png" for name in CAPTURE_IDS} and
            not (root / "material-settings-unwritten.json").exists(), "Native file-kind/settings inventory differs")
    fixtures = data["fixtures"]
    require(len(fixtures) == 5 and [r["file"] for r in fixtures] == SAVE_NAMES, "Authored fixture list differs")
    for index, row in enumerate(fixtures):
        tick = 21600 if index == 1 else 7200
        calendar = tick + (30 * 28800 if index == 4 else 0)
        require(row["tick"] == tick and row["calendar_tick"] == calendar and
                row["season"] == (calendar // (10 * 28800)) % 4 and row["authored_fixture"] is True and
                row["earned_history"] is False and type(row["checksum"]) is int and 0 < row["checksum"] < 2**64 and
                (root / row["file"]).stat().st_size > 0, "Authored calendar/cover fixture misrepresented")
    require(len({r["checksum"] for r in fixtures}) == 5, "Five differently authored fixture checksums are vacuous")
    hardware = data["hardware"]
    require(hardware["backend"] == "direct3d12" and hardware["software"] is False and hardware["graph"] == "LegacyCombined" and
            hardware["resolution"] == [1920, 1080] and hardware["gpu"] and hardware["driver"] and
            hardware["fps"] is None and hardware["gpu_execution_ms"] is None, "Native route/performance scope differs")
    world, art = load(original / "art/world.json"), load(material / "art/world.json")
    metadata = load(material / "art/material-collection.json")
    inputs = data["inputs"]
    require(world["schema"] == 1 and inputs["original_pages"] == len(world["pages"]) == 67 and
            inputs["original_records"] == sum(len(p["sprites"]) for p in world["pages"]) == 987 and
            inputs["original_world_sha256"] == digest(original / "art/world.json") and
            inputs["collection_world_sha256"] == digest(material / "art/world.json") and
            inputs["collection_metadata_sha256"] == digest(material / "art/material-collection.json") and
            inputs["original_content_hash"] == manifest_content(load(original / "manifest.json")) and
            inputs["collection_content_hash"] == manifest_content(load(material / "manifest.json")) and
            valid_sha(inputs["ui_content_hash"]), "Input source/catalog identities differ")
    verify_registrations(original, material, world, data, metadata, art)
    verify_negatives(root, material, metadata, art, data["cold_load_rejections"])
    captures = data["captures"]
    require([r["id"] for r in captures] == list(CAPTURE_IDS), "Capture sequence differs")
    by_id, images = {}, {}
    for capture in captures:
        identity = capture["id"]
        path = root / (identity + ".png")
        require(capture["file"] == path.name and capture["bytes"] == path.stat().st_size and
                capture["sha256"] == digest(path) and capture["width"] == 1920 and capture["height"] == 1080 and
                valid_sha(capture["frame_sha256"]), "Native capture linkage differs")
        with Image.open(path) as image:
            require(image.size == (1920, 1080), "Native decoded PNG dimensions differ")
            images[identity] = image.convert("RGBA")
        by_id[identity] = capture
        verify_frame_contract(capture)
        telemetry = capture["telemetry"]
        require(all(telemetry[k] == 0 for k in ("occluders", "occluder_upload_bytes", "contact_casters", "contact_draws",
                "contact_mask_binds", "water_draws", "reflection_draws")) and
                telemetry["graph_passes"] == ([4] if capture["frame_contract"]["content"] == 1 else []),
                "LegacyCombined/UiOnly executed cumulative world work")

    def changed(first: str, second: str, bounds: list | None = None) -> int:
        channels = ImageChops.difference(images[first], images[second]).split()
        mask = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]),
                                 ImageChops.lighter(channels[2], channels[3]))
        if bounds is not None:
            x, y, w, h = bounds
            box = (max(0, math.ceil(x - .5)), max(0, math.ceil(y - .5)),
                   min(1920, math.ceil(f32(x + w) - .5)), min(1080, math.ceil(f32(y + h) - .5)))
            require(box[0] < box[2] and box[1] < box[3], "Native comparison bounds are off-screen/vacuous")
            mask = mask.crop(box)
        histogram = mask.histogram()
        return mask.width * mask.height - histogram[0]

    def frame(identity: str) -> dict:
        return by_id[identity]["frame_contract"]

    def frame_sha(identity: str) -> str:
        return by_id[identity]["frame_sha256"]

    zooms = data["zoom_cases"]
    require(len(zooms) == 3 and [r["id"] for r in zooms] == ["default", "min", "max"], "Zoom controls differ")
    for row, zoom, wheel in zip(zooms, (f32(1.3), f32(.62), 3.), (0., -100., 100.), strict=True):
        require(row["zoom"] == zoom and row["wheel_delta"] == wheel and row["typed_material_draws"] == 2,
                "Ordinary wheel/clamp control differs")
        legacy, mixed = "material-legacy-" + row["id"], "material-mixed-" + row["id"]
        for name, key, entity, position, offset in (("house", KEYS[0], 2, [26., 22.], 15.),
                                                   ("resident", KEYS[1], 1, [21., 27.], 6.)):
            query = row[name]
            require(query["key"] == key and query["entity_id"] == entity and query["building"] is (name == "house") and
                    query["pick_scope"] == "conservative max-pooled mask, longest side <=192, threshold64, half-open bounds",
                    "Zoom query identity/scope differs")
            close_values(query["ground"], projected_ground(position, zoom, offset))
            close_values(query["camera_origin"], projected_ground([24., 24.], zoom, 0.))
            verify_query(original, world, query, zoom)
            roles = frame(mixed)["typed_roles"]
            require(any(r["command"] == query["mixed"]["command"] and r["draw"] == query["mixed"]["draw"] for r in roles),
                    "Query quad is not the actual captured typed role")
            surface = query["mixed"]["surface"]
            lit = next(s for s in art["lit_sprites"] if s["id"] == "dynamic." + key)
            close_values(surface["depth_scale"][:2], lit["depth_range"], .000001)
            close_values(surface["response"], [0., .08, 0., 0.], .000001)
            require(surface["receiver_owner"] == 0 and surface["depth_scale"][3] == 0., "Unexpected cumulative sprite metadata")
        require(row["whole_changed_pixels"] == changed(legacy, mixed) > 0 and
                row["house_bounds_changed_pixels"] == changed(legacy, mixed, row["house"]["bounds"]) > 0 and
                row["resident_bounds_changed_pixels"] == changed(legacy, mixed, row["resident"]["bounds"]) > 0,
                "Whole-route native image difference is absent or misreported")

    authority = data["authority_cases"]
    require([r["id"] for r in authority] == ["night", "wet", "mine", "unavailable"], "Authority controls differ")
    for row, tick, wet in zip(authority[:2], (21600, 7200), (0., f32(.6005)), strict=True):
        weather = row["weather"]
        require(weather["known"] is True and weather["evaluated_tick"] == weather["calendar_tick"] == tick and
                weather["offset"] == 0 and weather["wet_permille"] == (600 if row["id"] == "wet" else 0) and
                weather["wet_remainder"] == (14400 if row["id"] == "wet" else 0) and weather["snow_permille"] == 0 and
                type(weather["kind"]) is int and 0 <= weather["kind"] <= 3 and
                type(weather["wind"]) is int and 0 <= weather["wind"] <= 1000 and
                type(weather["temperature_deci_c"]) is int and -800 <= weather["temperature_deci_c"] <= 600 and
                weather["precipitation"] in (0, 1, 2), "Authored known-weather/calendar sample differs")
        event = weather["kind"] in (2, 3)
        require((weather["precipitation"] != 0) == event and
                (not event or ((weather["precipitation"] == 2) == (weather["temperature_deci_c"] <= 0))),
                "Weather phase/temperature semantics differ")
        hour = f32((6. + tick / 1200.) % 24.)
        close_values([row["hour"], row["wetness"]], [hour, wet], .000001)
        for name, expected in zip(("ambient", "sun_direction", "sun_color_intensity"), lighting(hour, weather["kind"]), strict=True):
            close_values(row[name], expected, .000003)
        mixed = "material-mixed-" + row["id"]
        legacy = "material-legacy-night" if row["id"] == "night" else "material-legacy-default"
        require(row["legacy_native_capture"] is (row["id"] == "night") and
                row["legacy_frame_sha256"] == frame_sha(legacy) and
                row["day_legacy_frame_sha256"] == frame_sha("material-legacy-default") and
                row["legacy_vs_mixed_changed_pixels"] == changed(legacy, mixed) > 0 and
                row["mixed_vs_day_changed_pixels"] == changed(mixed, "material-mixed-default") > 0 and
                row["legacy_vs_day_changed_pixels"] == changed(legacy, "material-legacy-default"),
                "Native authority route comparison differs")
    require(authority[1]["legacy_vs_day_changed_pixels"] == 0, "Wet-only fixture changed the legacy baseline")
    mine, unavailable = authority[2:]
    require(mine["hdr"] is False and mine["typed_material_draws"] == 0 and mine["pixels_equal"] is True and
            valid_sha(mine["mixed_frame_sha256"]) and mine["mixed_frame_sha256"] == mine["baseline_frame_sha256"] and
            mine["vertices"] == frame("material-mine-fallback")["vertex_count"] and
            mine["commands"] == frame("material-mine-fallback")["command_count"] and
            changed("material-mine-fallback", "material-mine-legacy") == 0, "Actual Mine fallback differs")
    require(unavailable["remote"] is True and all(unavailable[k] is False for k in ("ready", "world_ready", "weather_known", "hdr")) and
            unavailable["typed_material_draws"] == 0 and valid_sha(unavailable["mixed_frame_sha256"]) and
            unavailable["mixed_frame_sha256"] == unavailable["baseline_frame_sha256"] and
            unavailable["scope"] == "actual pending GNS connection; recovery_incomplete/reconnecting routes remain OPEN",
            "Pending transport fallback scope/published equivalence differs")
    empty = frame("material-authority-unavailable")
    require(all(empty[k] == 0 for k in ("vertex_count", "index_count", "command_count", "sprite_material_count",
            "companion_count")) and images["material-authority-unavailable"].getextrema() ==
            ((139, 139), (158, 158), (171, 171), (255, 255)),
            "Pending no-world authority did not produce the original uniform clear-only fallback")

    variants = data["variants"]
    expected_variants = [("level2", 2, [26., 22.]), ("stage1", 4, [19., 29.]),
                         ("stage2", 5, [22., 29.]), ("stage3", 6, [25., 29.]), ("winter", 2, [26., 22.])]
    require(len(variants) == 5, "Original variant coverage differs")
    for row, (suffix, entity, position) in zip(variants, expected_variants, strict=True):
        require(row["key"] == "building.house." + suffix and row["entity_id"] == entity and row["world"] == position and
                row["registered"] is False and row["tick"] == 7200, "Unrelated stage/tier/season was aliased")
        verify_legacy_quad(original, world, row, f32(76. * f32(2.1)), f32(1.3), position, 15.)
    animation = data["animation_cases"]
    require(len(animation) == 4 and {r["phase"] for r in animation} == {0, 1, 2, 3} and
            frame("material-animation-fallback")["sprite_material_count"] == 1, "Actual single-direction phase fallback is vacuous")
    previous_step, previous_y = 0, 27.
    for row in animation:
        require(row["facing"] == 3 and row["key"] == f"resident.man.walk.3.{row['phase']}" and
                row["moving"] is True and row["mapped"] is False and type(row["step"]) is int and
                previous_step < row["step"] <= 120 and row["authority_tick"] == 7200 + row["step"],
                "Actual movement/phase fixture identity differs")
        # Real local MotionClock delivery is one tick behind current authority;
        # straight +Y movement allows direct position-derived cadence checks.
        close_values([row["shown_tick"]], [row["authority_tick"] - 1.], .000001)
        ax, ay = row["authority_position"]
        px, py = row["displayed_position"]
        require(ax == px == 21. and previous_y < py <= ay <= 35. and py >= 27. and ay - py <= .12501,
                "Shown movement advanced ahead of authority or lost its declared axis")
        phase_position = ((py - 27.) / 2.) % f32(.9)
        require(int(phase_position / f32(.9) * 4.) % 4 == row["phase"], "Actual phase differs from displayed-distance cadence")
        verify_legacy_quad(original, world, row, 39., f32(1.3), row["displayed_position"], 6.)
        previous_step, previous_y = row["step"], py

    install, late = data["cold_install"], data["post_submit_rejection"]
    require(install["registered_count"] == 2 and install["before_any_submission"] is True and
            valid_sha(install["baseline_frame_sha256"]) and
            install["mixed_frame_sha256"] == data["cold_load_rejections"][0]["before_frame_sha256"] and
            late["id"] == "after_actual_rml_menu" and
            late["error"] == "Normal material collection installation is startup-only before any submission" and
            late["before_frame_sha256"] == late["after_frame_sha256"] == install["mixed_frame_sha256"] and
            late["selected_buildings_before"] == late["selected_buildings_after"] == [2] and
            late["registered_count"] == 2 and late["submitted"] is True, "Cold/actual-post-submit transaction differs")
    ui = data["ui"]
    require(set(ui) == {"menu_first", "hud", "selection", "interleave"}, "Real UI scope differs")
    for name, capture in (("menu_first", "material-menu-first"), ("hud", "material-world-hud")):
        row, counters = ui[name], frame(capture)
        require(row["vertices"] == counters["vertex_count"] and row["indices"] == counters["index_count"] and
                row["commands"] == counters["command_count"] and row["surface_vertices"] == counters["companion_count"] and
                0 < row["premultiplied_draws"] <= row["ui_draws"] and
                2 <= row["distinct_textures"] <= row["textured_draws"] <= row["ui_draws"], "Real RmlUi font/image suffix is vacuous")
    require(frame("material-menu-first")["content"] == frame("material-menu-interleave")["content"] == 1 and
            ui["menu_first"]["ui_draws"] == frame("material-menu-first")["command_count"], "Menu is not explicit UiOnly")
    clean, hud, selected = frame("material-mixed-default"), frame("material-world-hud"), frame("material-world-selected")
    try:
        visible_world(bytes((0, 0, 0, 255)) * (1920 * 1080))
    except AssertionError:
        pass
    else:
        raise AssertionError("Opaque black World negative oracle was accepted")
    nonblack, unique, nonmodal = visible_world(images["material-mixed-default"].tobytes())
    # Same bound UI-kit HUD as the accepted UI scene oracle: the central
    # rectangle excludes its top/bottom/right panels for this unselected scene.
    # This proves retained visible scene pixels, not every panel's clip coverage.
    region = (280, 100, 1590, 850)
    world_region = images["material-mixed-default"].crop(region).tobytes()
    require(world_region == images["material-world-hud"].crop(region).tobytes(),
            "HUD replaced the declared central World witness")
    central_nonblack = sum(any(world_region[index:index + 3]) for index in range(0, len(world_region), 4))
    central_colors = Counter(zip(world_region[0::4], world_region[1::4], world_region[2::4], world_region[3::4]))
    central_nonmodal = len(world_region) // 4 - max(central_colors.values())
    require(central_nonblack > 10000 and len(central_colors) > 64 and central_nonmodal > 10000,
            "Central World witness is uniformly clear or vacuous")
    require(hud["vertex_count"] > clean["vertex_count"] and hud["index_count"] > clean["index_count"] and
            ui["hud"]["ui_draws"] == hud["command_count"] - clean["command_count"] and
            ui["hud"]["world_prefix_sha256"] == frame_sha("material-world-restored") == frame_sha("material-mixed-default") and
            ui["hud"]["changed_pixels"] == changed("material-mixed-default", "material-world-hud") > 1000,
            "Actual HUD did not preserve and visibly extend the clean World")
    selection = ui["selection"]
    require(selection["selected_buildings"] == [2] and selection["typed_material_draws"] == 2 and
            selection["clean_vertices"] == clean["vertex_count"] < selection["selected_vertices"] == selected["vertex_count"] and
            selection["clean_commands"] == clean["command_count"] < selection["selected_commands"] == selected["command_count"] and
            selection["changed_pixels"] == changed("material-mixed-default", "material-world-selected") > 0 and
            selection["outline_scope"] == "original legacy paint outline; typed geometry outline remains OPEN",
            "Selected original outline/annotation is absent or overclaimed")
    close_values(selection["point"], zooms[0]["house"]["samples"][0]["point"])
    interleave = ui["interleave"]
    require(interleave["menu_pixels_equal"] is True and interleave["world_pixels_equal"] is True and
            interleave["before_frame_sha256"] == interleave["after_frame_sha256"] == frame_sha("material-world-restored") and
            changed("material-menu-first", "material-menu-interleave") == 0 and
            changed("material-mixed-default", "material-world-restored") == 0, "World/UI/World did not repeat exactly")
    return {"captures": 20, "zoom_queries": 6, "isolated_catalog_rejections": 6, "original_variants": 5,
            "actual_animation_phases": 4, "authority_cases": 4, "authored_world_saves": 5,
            "world_nonblack_pixels": nonblack, "world_unique_rgba": unique,
            "world_nonmodal_pixels": nonmodal, "central_unique_rgba": len(central_colors),
            "central_world_hud_exact": True, "central_nonblack_pixels": central_nonblack,
            "central_nonmodal_pixels": central_nonmodal,
            "pixel_scope": "whole-route comparisons", "seeded_weather_replay": False, "fps": None}
