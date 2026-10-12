#!/usr/bin/env python3
"""Standalone retained-artifact tests for export_engine_control_kit.py."""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import math
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path


EXPECTED_IDS = {"control.house", "control.stockpile", "control.workshop", "control.tree",
                "control.rock", "control.resident.empty", "control.resident.log",
                "control.resident.sack"}
MIN_FREE_BYTES = 384 * 1024 * 1024
MAX_RETAINED_BYTES = 128 * 1024 * 1024
WET_PROBE_BYTES = {"control.house": 255, "control.stockpile": 204, "control.workshop": 204,
                   "control.tree": 255, "control.rock": 230, "control.resident.empty": 166,
                   "control.resident.log": 166, "control.resident.sack": 0}


def check(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def float32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def files(root: Path) -> dict[str, bytes]:
    return {path.relative_to(root).as_posix(): path.read_bytes()
            for path in sorted(root.rglob("*")) if path.is_file()}


def retained_bytes(root: Path) -> int:
    return sum(path.stat().st_size for path in root.rglob("*") if path.is_file())


def connected_regions(mask: bytes, width: int, height: int) -> int:
    active = {index for index, value in enumerate(mask) if value != 0}
    regions = 0
    while active:
        regions += 1
        pending = [active.pop()]
        while pending:
            index = pending.pop()
            x, y = index % width, index // width
            neighbors = []
            if x > 0:
                neighbors.append(index - 1)
            if x + 1 < width:
                neighbors.append(index + 1)
            if y > 0:
                neighbors.append(index - width)
            if y + 1 < height:
                neighbors.append(index + width)
            for neighbor in neighbors:
                if neighbor in active:
                    active.remove(neighbor)
                    pending.append(neighbor)
    return regions


def run_export(python: str, exporter: Path, output: Path, success: bool) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([python, str(exporter), str(output)], text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False,
                            env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
    check((result.returncode == 0) == success,
          f"unexpected exporter status {result.returncode}: {result.stdout}{result.stderr}")
    return result


def load_exporter(path: Path):
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(path.parent))
    specification = importlib.util.spec_from_file_location("engine_control_kit_under_test", path)
    check(specification is not None and specification.loader is not None, "cannot load exporter")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def decode_oct(x_byte: int, y_byte: int) -> tuple[float, float, float]:
    x, y = x_byte / 255.0 * 2.0 - 1.0, y_byte / 255.0 * 2.0 - 1.0
    z = 1.0 - abs(x) - abs(y)
    if z < 0.0:
        old_x = x
        x = (1.0 - abs(y)) * (1.0 if old_x >= 0.0 else -1.0)
        y = (1.0 - abs(old_x)) * (1.0 if y >= 0.0 else -1.0)
    length = math.sqrt(x * x + y * y + z * z)
    return x / length, y / length, z / length


def reconstruct(pixel: list[float], encoded_depth: int, sprite: dict) -> tuple[float, float, float]:
    eppp = sprite["export_pixels_per_reference_pixel"]
    u = (pixel[0] - sprite["pivot_px"][0]) / eppp
    v = (pixel[1] - sprite["pivot_px"][1]) / eppp
    depth_min, depth_max = sprite["depth_range"]
    depth = depth_min + encoded_depth / 65535.0 * (depth_max - depth_min)
    total = depth * math.sqrt(3.0)
    difference = u / 29.0
    half_sum_minus_z = v / 29.0
    coordinate_sum = (half_sum_minus_z + total) / 1.5
    z = total - coordinate_sum
    return (coordinate_sum + difference) / 2.0, (coordinate_sum - difference) / 2.0, z


def validate_wet_policy(source_masters: tuple[dict, ...]) -> None:
    # Independent literal eligibility, including dry bottom/skin/window controls.
    exterior = {"house.body": .8, "house.roof": 1., "stockpile.base": .75,
                "stockpile.stack.left": .8, "stockpile.stack.right": .8,
                "workshop.body": .8, "workshop.chimney": .9, "workshop.awning": 1.,
                "tree.trunk": .9, "tree.crown.low": 1., "tree.crown.high": 1.,
                "rock.base": .9, "rock.crown": .9, "resident.empty.body": .65,
                "resident.log.body": .65, "resident.sack.body": .65,
                "resident.log.cargo": .85, "resident.sack.cargo": .6}
    for master in source_masters:
        for triangle in master["triangles"]:
            identifier = triangle["id"]
            dry = triangle["emission_mask"] > 0 or ".bottom." in identifier or ".head." in identifier
            expected = [value for prefix, value in exterior.items() if identifier.startswith(prefix + ".")]
            check(dry or len(expected) == 1, f"unclassified diagnostic exterior: {identifier}")
            check(triangle["wet_mask"] == (0. if dry else expected[0]),
                  f"literal wet-mask eligibility changed: {identifier}")


def validate_catalog(root: Path, source_masters: tuple[dict, ...]) -> None:
    validate_wet_policy(source_masters)
    world = json.loads((root / "art/world.json").read_text(encoding="utf-8"))
    metadata = json.loads((root / "art/control-kit.json").read_text(encoding="utf-8"))
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    check(world["schema"] == 2 and world["pages"] == [], "startup catalog is not schema 2")
    check(metadata["purpose"] == "diagnostic-neutral-controls-only" and
          metadata["production_catalog"] is False and metadata["geometry_scale"] == 1.0 and
          metadata["wet_mask_policy"] == "diagnostic-exterior-v1",
          "diagnostic-only metadata contract changed")
    categories = {row["id"]: row for row in metadata["categories"]}
    check(set(categories) == EXPECTED_IDS and metadata["category_count"] == 8,
          "control category IDs are incomplete or not distinct")
    check(len(world["textures"]) == 48 and len(world["materials"]) == 8 and
          len(world["lit_sprites"]) == 8, "schema-2 descriptor cardinality changed")
    textures = {row["id"]: row for row in world["textures"]}
    materials = {row["id"]: row for row in world["materials"]}
    sprites = {row["id"]: row for row in world["lit_sprites"]}
    contact_arts = {row["id"]: row for row in world["contact_arts"]}
    source_by_id = {row["diagnostic_category"]: row for row in source_masters}
    check(len(world["contact_arts"]) == len(contact_arts) == metadata["contact_art_count"] == 10,
          "contact art cardinality must be eight category bases and two independent cargo footprints")
    check(not ({row["path"] for row in contact_arts.values()} &
               {row["path"] for row in textures.values()}),
          "contact art must have independent receiver-plane masks, not sprite channel bindings")
    for art in contact_arts.values():
        check(set(art) == {"id", "path", "format", "layout", "width", "height", "bytes",
                          "sha256", "region", "facing"} and
              art["format"] == "r8_unorm" and art["layout"] == "tight_le" and
              art["width"] == art["height"] == 32 and art["bytes"] == 1024 and
              art["region"] == [0, 0, 32, 32] and art["facing"] == "omnidirectional" and
              art["path"].endswith(".rtex"), "contact art typed descriptor is invalid")
        mask = (root / art["path"]).read_bytes()
        check(len(mask) == art["bytes"] and digest(mask) == art["sha256"] and
              any(mask) and connected_regions(mask, 32, 32) == 1,
              f"contact art interior/hash/ledger is invalid: {art['id']}")
        check(not any(mask[:32]) and not any(mask[-32:]) and
              not any(mask[0::32]) and not any(mask[31::32]),
              f"contact art requires a transparent outer ring: {art['id']}")
        check(all(mask[y * 32 + x] == mask[y * 32 + 31 - x] == mask[(31 - y) * 32 + x]
                  for y in range(32) for x in range(32)),
              f"contact art footprint is asymmetric: {art['id']}")
    check(len({row["silhouette_sha256"] for row in categories.values()}) == 8,
          "category silhouettes are not pairwise distinct")
    for row in manifest["assets"]:
        payload = (root / row["path"]).read_bytes()
        check(row["bytes"] == len(payload) and row["sha256"] == digest(payload),
              f"manifest ledger mismatch for {row['id']}")
        check(row["source"] == "export_engine_control_kit.py" and row["notice"] == "NOTICE.txt",
              "manifest provenance is incomplete")
    for category_id, record in categories.items():
        sprite = sprites[record["sprite_id"]]
        material = materials[record["material_id"]]
        check(sprite["local_emitters"] == record["local_emitters"],
              f"local-emitter metadata mismatch for {category_id}")
        source = source_by_id[category_id]
        contacts = sprite["contact_shadows"]
        expected_count = 2 if category_id in {"control.resident.log", "control.resident.sack"} else 1
        check(len(contacts) == len(record["contact_arts"]) == expected_count and
              contacts == record["contact_shadows"] and
              record["contact_source_geometry"] == source["contact_footprints"],
              f"contact footprint source/cardinality mismatch for {category_id}")
        for contact, footprint, art in zip(contacts, source["contact_footprints"], record["contact_arts"]):
            expected_id = f"{category_id}.contact.{footprint['id']}"
            points = [vertex for triangle in source["triangles"]
                      if triangle["id"].startswith(footprint["geometry_prefix"] + ".")
                      for vertex in triangle["vertices"]]
            low = [min(point[axis] for point in points) for axis in range(2)]
            high = [max(point[axis] for point in points) for axis in range(2)]
            expected_contact = {
                "art": expected_id,
                "anchor_local": [float32((low[axis] + high[axis]) / 2.0) for axis in range(2)] + [0.0],
                "half_extent": [float32((high[axis] - low[axis]) / 2.0 + float32(footprint["margin"]))
                                for axis in range(2)],
                "pivot": [0.5, 0.5], "opacity": float32(footprint["opacity"]),
                "plane_tolerance": float32(footprint["plane_tolerance"]),
            }
            check(contact == expected_contact and art == contact_arts[expected_id],
                  f"contact footprint does not follow its authored geometry: {expected_id}")
        if expected_count == 2:
            check(contacts[1]["art"].endswith(".cargo") and
                  contacts[1]["anchor_local"] != contacts[0]["anchor_local"] and
                  contacts[1]["half_extent"] != contacts[0]["half_extent"],
                  f"held cargo contact is not independently authored for {category_id}")
        check(sprite["source_size"] == [float(axis) for axis in record["source_size"]] and
              sprite["pivot_px"] == record["pivot_px"] and
              sprite["depth_range"] == record["depth_range"],
              f"sprite geometry metadata mismatch for {category_id}")
        check(record["sky_shadow_triangle_count"] == record["triangle_count"] and
              len(sprite["sky_shadow_triangles"]) == record["triangle_count"] and
              all(triangle["source_mask"] == 3 for triangle in sprite["sky_shadow_triangles"]),
              f"sky geometry is not full mask-3 geometry for {category_id}")
        channels = material["channels"]
        check(set(channels) == {"albedo", "normal_material", "camera_depth", "emission_mask",
                                "surface_mask", "caster_mask"}, f"six-channel contract changed for {category_id}")
        sizes = set()
        payloads: dict[str, bytes] = {}
        for channel, binding in channels.items():
            texture = textures[binding["texture"]]
            check(binding["rect"] == [0, 0, texture["width"], texture["height"]],
                  f"channel rectangle mismatch for {category_id}.{channel}")
            payload = (root / texture["path"]).read_bytes()
            check(len(payload) == texture["bytes"] and digest(payload) == texture["sha256"],
                  f"texture ledger mismatch for {category_id}.{channel}")
            sizes.add((texture["width"], texture["height"]))
            payloads[channel] = payload
        check(len(sizes) == 1, f"channel dimensions diverge for {category_id}")
        caster = payloads["caster_mask"]
        emission = payloads["emission_mask"]
        check(set(caster) <= {0, 255} and 255 in caster and digest(caster) == record["silhouette_sha256"],
              f"caster silhouette is invalid for {category_id}")
        albedo_alpha = payloads["albedo"][3::4]
        surface_alpha = payloads["surface_mask"][3::4]
        check(all((mask == 255) == (alpha == 255) == (surface == 255)
                  for mask, alpha, surface in zip(caster, albedo_alpha, surface_alpha)) and
              all(not value or caster[index] == 255 for index, value in enumerate(emission)),
              f"opaque, surface, caster, and emission masks diverge for {category_id}")
        wet = payloads["surface_mask"][0::4]
        check(any(value > 0 for value in wet) and
              all(not value or caster[index] == 255 for index, value in enumerate(wet)) and
              all(value == 0 for value in payloads["surface_mask"][1::4] + payloads["surface_mask"][2::4]),
              f"visible exterior wet-mask carrier is invalid for {category_id}")
        check(all(not emitted or wet[index] == 0 for index, emitted in enumerate(emission)),
              f"emissive window acquired wetness for {category_id}")
        check(record["probe_scope"] == "one-representative-visible-face" and
              len(record["probes"]) == 12 and
              all(probe["boundary_clearance_px"] >= 8.0 for probe in record["probes"]),
              f"representative-face normal/depth probe coverage is unsafe for {category_id}")
        width = record["source_size"][0]
        for probe in record["probes"]:
            x, y = int(probe["source_pixel"][0]), int(probe["source_pixel"][1])
            pixel = y * width + x
            check(wet[pixel] == WET_PROBE_BYTES[category_id],
                  f"literal exported wet-mask byte differs for {category_id}.{probe['id']}")
            normal = decode_oct(payloads["normal_material"][pixel * 4],
                                payloads["normal_material"][pixel * 4 + 1])
            check(sum(a * b for a, b in zip(normal, probe["normal"])) > 0.9998,
                  f"normal channel does not match geometry for {category_id}.{probe['id']}")
            encoded_depth = struct.unpack_from("<H", payloads["camera_depth"], pixel * 2)[0]
            position = reconstruct(probe["source_pixel"], encoded_depth, sprite)
            check(max(abs(a - b) for a, b in zip(position, probe["position"])) < 2.0e-4,
                  f"depth/pivot reconstruction mismatch for {category_id}.{probe['id']}")
        if category_id == "control.house":
            expected_emitters = [
                {"id": "house.window.left",
                 "anchor_xyz": [float32(-0.52), float32(0.93), float32(0.88)],
                 "radius": float32(4.5),
                 "direction_cone": [float32(0.0), float32(0.70710677),
                                    float32(-0.70710677), float32(0.2)],
                 "inner_cosine": float32(0.7), "falloff": float32(2.0), "receiver_mask": 1},
                {"id": "house.window.right",
                 "anchor_xyz": [float32(0.52), float32(0.93), float32(0.88)],
                 "radius": float32(4.5),
                 "direction_cone": [float32(0.0), float32(0.70710677),
                                    float32(-0.70710677), float32(0.2)],
                 "inner_cosine": float32(0.7), "falloff": float32(2.0), "receiver_mask": 1},
            ]
            check(record["emission_windows"] == ["house.window.left", "house.window.right"] and
                  record["local_emitters"] == expected_emitters and
                  all(emitter["anchor_xyz"][1] > 0.9 for emitter in record["local_emitters"]) and
                  connected_regions(emission, width, record["source_size"][1]) == 2 and
                  material["emission"] != [0.0, 0.0, 0.0, 0.0],
                  "house does not have exactly two eligible authored window emitters")
        else:
            check(record["emission_windows"] == [] and record["local_emitters"] == [] and
                  set(emission) <= {0} and
                  material["emission"] == [0.0, 0.0, 0.0, 0.0],
                  f"non-house category emits unexpectedly: {category_id}")


def assert_invalid_metadata(exporter, root: Path, name: str, mutate, diagnostic: str) -> None:
    invalid = copy.deepcopy(exporter.category_masters()[0])
    mutate(invalid)
    destination = root / f"invalid-local-emitter-{name}"
    try:
        exporter.export(destination, [invalid])
    except exporter.ControlKitError as error:
        check(str(error) == diagnostic, f"invalid local-emitter {name} diagnostic changed: {error}")
    else:
        raise AssertionError(f"invalid local-emitter {name} metadata was not rejected")
    check(not destination.exists(), f"invalid local-emitter {name} created its requested output root")


def assert_invalid_contact(exporter, root: Path, name: str, mutate, diagnostic: str) -> None:
    invalid = copy.deepcopy(exporter.category_masters()[0])
    mutate(invalid)
    destination = root / f"invalid-contact-{name}"
    try:
        exporter.export(destination, [invalid])
    except exporter.ControlKitError as error:
        check(str(error) == diagnostic, f"invalid contact {name} diagnostic changed: {error}")
    else:
        raise AssertionError(f"invalid contact {name} metadata was not rejected")
    check(not destination.exists(), f"invalid contact {name} created its requested output root")


def mutate_contact_geometry(master: dict, *, collapse: bool = False) -> None:
    prefix = master["contact_footprints"][0]["geometry_prefix"] + "."
    master["contact_footprints"][0]["margin"] = 0.0
    for triangle in master["triangles"]:
        if triangle["id"].startswith(prefix):
            for point in triangle["vertices"]:
                point[0] = 0.0 if collapse else point[0] + 20000.0
                if collapse:
                    point[1] = 0.0


def validate_contact_negatives(exporter, root: Path) -> None:
    label = "control.house contact_footprints[0]"
    cases = (
        ("container", lambda master: master.__setitem__("contact_footprints", {}),
         "control.house contact_footprints must contain between 1 and 4 entries"),
        ("empty", lambda master: master.__setitem__("contact_footprints", []),
         "control.house contact_footprints must contain between 1 and 4 entries"),
        ("capacity", lambda master: master.__setitem__("contact_footprints", master["contact_footprints"] * 5),
         "control.house contact_footprints must contain between 1 and 4 entries"),
        ("fields", lambda master: master["contact_footprints"][0].__setitem__("glow", 1.0),
         f"{label} must contain exactly the authored contact-footprint fields"),
        ("record-type", lambda master: master.__setitem__("contact_footprints", [True]),
         f"{label} must contain exactly the authored contact-footprint fields"),
        ("unsafe-id", lambda master: master["contact_footprints"][0].__setitem__("id", "../mask"),
         f"{label}.id must be a safe lowercase contact identifier"),
        ("duplicate-id", lambda master: master.__setitem__("contact_footprints", master["contact_footprints"] * 2),
         "control.house duplicate contact footprint id: base"),
        ("unknown-shape", lambda master: master["contact_footprints"][0].__setitem__("shape", "sprite"),
         f"{label}.shape must be box or ellipse"),
        ("shape-type", lambda master: master["contact_footprints"][0].__setitem__("shape", []),
         f"{label}.shape must be box or ellipse"),
        ("prefix-type", lambda master: master["contact_footprints"][0].__setitem__("geometry_prefix", False),
         f"{label}.geometry_prefix must name authored geometry"),
        ("unknown-geometry", lambda master: master["contact_footprints"][0].__setitem__("geometry_prefix", "unknown"),
         f"{label}.geometry_prefix has no authored triangles"),
        ("margin-bool", lambda master: master["contact_footprints"][0].__setitem__("margin", True),
         f"{label}.margin must be a finite number"),
        ("margin-bound", lambda master: master["contact_footprints"][0].__setitem__("margin", 0.6),
         f"{label} margin/opacity is outside its contact bound"),
        ("float32-overflow", lambda master: master["contact_footprints"][0].__setitem__("margin", 1.0e300),
         f"{label}.margin is outside the finite float32 domain"),
        ("opacity-nonfinite", lambda master: master["contact_footprints"][0].__setitem__("opacity", math.nan),
         f"{label}.opacity must be a finite number"),
        ("opacity-bound", lambda master: master["contact_footprints"][0].__setitem__("opacity", -0.01),
         f"{label} margin/opacity is outside its contact bound"),
        ("plane-bound", lambda master: master["contact_footprints"][0].__setitem__("plane_tolerance", 0.041),
         f"{label}.plane_tolerance must be within [0.02, 0.04]"),
        ("triangle-record", lambda master: master["triangles"].__setitem__(0, False),
         f"{label}.geometry must use authored triangle records"),
        ("triangle-vertices", lambda master: master["triangles"][0].__setitem__("vertices", [[0.0, 0.0, 0.0]]),
         f"{label}.geometry triangle must have exactly three vertices"),
        ("geometry-bound", lambda master: mutate_contact_geometry(master),
         f"{label} geometry-derived footprint exceeds its local bound"),
        ("geometry-degenerate", lambda master: mutate_contact_geometry(master, collapse=True),
         f"{label} geometry-derived footprint exceeds its local bound"),
    )
    for name, mutate, diagnostic in cases:
        assert_invalid_contact(exporter, root, name, mutate, diagnostic)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exporter", type=Path)
    parser.add_argument("output_root", type=Path)
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args()
    if args.output_root.exists():
        raise AssertionError("test output root already exists; tests refuse to overwrite")
    if not args.output_root.parent.is_dir():
        raise AssertionError("test output parent must already exist")
    free_bytes = shutil.disk_usage(args.output_root.parent).free
    if free_bytes < MIN_FREE_BYTES:
        raise AssertionError(f"test volume has {free_bytes} free bytes; {MIN_FREE_BYTES} required")
    os.mkdir(args.output_root)
    first, second = args.output_root / "export-a", args.output_root / "export-b"
    run_export(args.python, args.exporter, first, True)
    first_bytes = retained_bytes(args.output_root)
    check(first_bytes * 2 + len(b"preserve-me\n") <= MAX_RETAINED_BYTES,
          "two retained exports would exceed the 128 MiB test-root budget")
    run_export(args.python, args.exporter, second, True)
    check(retained_bytes(args.output_root) <= MAX_RETAINED_BYTES,
          "retained test root exceeds 128 MiB after deterministic re-export")
    check(files(first) == files(second), "re-export is not byte-exact")
    exporter = load_exporter(args.exporter)
    source_masters = exporter.category_masters()
    validate_catalog(first, source_masters)
    validate_contact_negatives(exporter, args.output_root)
    existing = args.output_root / "existing-output"
    os.mkdir(existing)
    sentinel = existing / "sentinel.txt"
    sentinel.write_bytes(b"preserve-me\n")
    check(retained_bytes(args.output_root) <= MAX_RETAINED_BYTES,
          "retained test root exceeds 128 MiB after rejection fixture creation")
    rejected = run_export(args.python, args.exporter, existing, False)
    check(rejected.returncode == 2 and rejected.stderr ==
          "engine control kit export failed: output root already exists; exporter refuses to overwrite\n" and
          sentinel.read_bytes() == b"preserve-me\n" and files(existing) == {"sentinel.txt": b"preserve-me\n"},
          "existing-output rejection mutated its destination or changed diagnostic")
    check(len(source_masters[0]["local_emitters"]) == 2 and
          {emitter["id"] for emitter in source_masters[0]["local_emitters"]} ==
          {"house.window.left", "house.window.right"} and
          all(master["local_emitters"] == [] for master in source_masters[1:]),
          "source masters must author only the two eligible house window emitters")
    point_master = copy.deepcopy(source_masters[0])
    point_master["local_emitters"][0]["direction_cone"][3] = -1.0
    point_master["local_emitters"][0]["inner_cosine"] = -1.0
    exporter.validate_categories([point_master])
    assert_invalid_metadata(
        exporter, args.output_root, "cardinality",
        lambda master: master.__setitem__("local_emitters", master["local_emitters"] * 3),
        "control.house local_emitters must be an array with at most 4 entries")
    assert_invalid_metadata(
        exporter, args.output_root, "fields",
        lambda master: master["local_emitters"][0].__setitem__("color", [1.0, 1.0, 1.0, 1.0]),
        "control.house local_emitters[0] must contain exactly the authored local-emitter fields")
    assert_invalid_metadata(
        exporter, args.output_root, "empty-id",
        lambda master: master["local_emitters"][0].__setitem__("id", ""),
        "control.house local_emitters[0].id must be a non-whitespace string of at most 256 UTF-8 bytes")
    assert_invalid_metadata(
        exporter, args.output_root, "long-id",
        lambda master: master["local_emitters"][0].__setitem__("id", "x" * 257),
        "control.house local_emitters[0].id must be a non-whitespace string of at most 256 UTF-8 bytes")
    assert_invalid_metadata(
        exporter, args.output_root, "duplicate-id",
        lambda master: master["local_emitters"][1].__setitem__(
            "id", master["local_emitters"][0]["id"]),
        "control.house duplicate local emitter id: house.window.left")
    assert_invalid_metadata(
        exporter, args.output_root, "anchor-bound",
        lambda master: master["local_emitters"][0].__setitem__("anchor_xyz", [8193.0, 0.93, 0.88]),
        "control.house local_emitters[0].anchor_xyz exceeds the local render bound")
    assert_invalid_metadata(
        exporter, args.output_root, "radius-bool",
        lambda master: master["local_emitters"][0].__setitem__("radius", True),
        "control.house local_emitters[0].radius must be a finite number")
    assert_invalid_metadata(
        exporter, args.output_root, "radius-nonfinite",
        lambda master: master["local_emitters"][0].__setitem__("radius", math.inf),
        "control.house local_emitters[0].radius must be a finite number")
    assert_invalid_metadata(
        exporter, args.output_root, "radius-bound",
        lambda master: master["local_emitters"][0].__setitem__("radius", 32.01),
        "control.house local_emitters[0].radius must be within (0, 32]")
    assert_invalid_metadata(
        exporter, args.output_root, "direction-unit",
        lambda master: master["local_emitters"][0].__setitem__("direction_cone", [0.0, 0.5, -0.5, 0.2]),
        "control.house local_emitters[0].direction_cone must have unit XYZ and outer cosine in [-1, 1]")
    assert_invalid_metadata(
        exporter, args.output_root, "direction-exact-tolerance",
        lambda master: master["local_emitters"][0].__setitem__(
            "direction_cone", [0.0, math.sqrt(1.0001001), 0.0, 0.2]),
        "control.house local_emitters[0].direction_cone must have unit XYZ and outer cosine in [-1, 1]")
    assert_invalid_metadata(
        exporter, args.output_root, "direction-axis-bound",
        lambda master: master["local_emitters"][0].__setitem__(
            "direction_cone", [1.00001, 0.0, 0.0, 0.2]),
        "control.house local_emitters[0].direction_cone must have unit XYZ and outer cosine in [-1, 1]")
    assert_invalid_metadata(
        exporter, args.output_root, "float32-overflow",
        lambda master: master["local_emitters"][0].__setitem__("radius", 1.0e300),
        "local emitter number is outside the finite float32 domain")
    assert_invalid_metadata(
        exporter, args.output_root, "inner-cone",
        lambda master: master["local_emitters"][0].__setitem__("inner_cosine", 0.2),
        "control.house local_emitters[0].inner_cosine must form a point pair at -1 or exceed outer cosine within [-1, 1]")
    assert_invalid_metadata(
        exporter, args.output_root, "falloff",
        lambda master: master["local_emitters"][0].__setitem__("falloff", 0.0),
        "control.house local_emitters[0].falloff must be within (0, 16]")
    assert_invalid_metadata(
        exporter, args.output_root, "receiver-mask-bool",
        lambda master: master["local_emitters"][0].__setitem__("receiver_mask", True),
        "control.house local_emitters[0].receiver_mask must be an integer within [1, 255]")
    assert_invalid_metadata(
        exporter, args.output_root, "inside-occluder",
        lambda master: master["local_emitters"][0].__setitem__("anchor_xyz", [-0.52, 0.89, 0.88]),
        "control.house local_emitters[0].anchor_xyz must clear its owner occluder volume by more than 0.002")
    assert_invalid_metadata(
        exporter, args.output_root, "on-occluder-boundary",
        lambda master: master["local_emitters"][0].__setitem__("anchor_xyz", [-0.52, 0.9, 0.88]),
        "control.house local_emitters[0].anchor_xyz must clear its owner occluder volume by more than 0.002")
    assert_invalid_metadata(
        exporter, args.output_root, "near-occluder-boundary",
        lambda master: master["local_emitters"][0].__setitem__("anchor_xyz", [-0.52, 0.901, 0.88]),
        "control.house local_emitters[0].anchor_xyz must clear its owner occluder volume by more than 0.002")
    assert_invalid_metadata(
        exporter, args.output_root, "near-occluder-top",
        lambda master: master["local_emitters"][0].__setitem__("anchor_xyz", [0.0, 0.0, 2.251]),
        "control.house local_emitters[0].anchor_xyz must clear its owner occluder volume by more than 0.002")
    assert_invalid_metadata(
        exporter, args.output_root, "zero-emission",
        lambda master: master.__setitem__("emission_radiance", [0.0, 0.0, 0.0, 1.0]),
        "control.house local emitters require nonzero effective material emission")
    try:
        exporter.validate_categories(list(exporter.category_masters()) * 5)
    except exporter.ControlKitError as error:
        check(str(error) == "category count must be within [1, 32]", "category cap diagnostic changed")
    else:
        raise AssertionError("category count cap was not enforced")
    oversized = dict(exporter.category_masters()[0])
    oversized["triangles"] = oversized["triangles"] * 20
    oversized["diagnostic_category"] = "control.invalid-cap"
    oversized["local_emitters"] = []
    try:
        exporter.validate_categories([oversized])
    except exporter.ControlKitError as error:
        check(str(error) == "control.invalid-cap triangle count must be within [1, 256]",
              "triangle cap diagnostic changed")
    else:
        raise AssertionError("per-category triangle cap was not enforced")
    for suffix, category_id in (("slash", "control.invalid/path"),
                                ("backslash", "control.invalid\\path")):
        invalid = dict(exporter.category_masters()[0])
        invalid["diagnostic_category"] = category_id
        invalid["local_emitters"] = []
        destination = args.output_root / f"invalid-separator-{suffix}"
        try:
            exporter.export(destination, [invalid])
        except exporter.ControlKitError as error:
            check(str(error) ==
                  "category[0] diagnostic_category must use safe lowercase ASCII control.* segments",
                  f"invalid {suffix} separator diagnostic changed")
        else:
            raise AssertionError(f"invalid {suffix} separator was not rejected")
        check(not destination.exists(), f"invalid {suffix} separator created its requested output root")
    collision_a = dict(exporter.category_masters()[0])
    collision_b = dict(exporter.category_masters()[1])
    collision_a["diagnostic_category"] = "control.a.b"
    collision_b["diagnostic_category"] = "control.a-b"
    collision_a["local_emitters"] = []
    collision_destination = args.output_root / "slug-collision"
    try:
        exporter.export(collision_destination, [collision_a, collision_b])
    except exporter.ControlKitError as error:
        check(str(error) == "diagnostic category path collision: control.a-b -> a-b",
              "derived-slug collision diagnostic changed")
    else:
        raise AssertionError("derived-slug collision was not rejected")
    check(not collision_destination.exists(), "slug collision created its requested output root")
    budget_destination = args.output_root / "byte-budget-rejected"
    original_budget = exporter.MAX_OUTPUT_BYTES
    exporter.MAX_OUTPUT_BYTES = 1
    try:
        try:
            exporter.export(budget_destination)
        except exporter.ControlKitError as error:
            check(str(error).endswith("exceeds 1 byte budget"), "byte-budget diagnostic changed")
        else:
            raise AssertionError("actual export byte budget was not enforced")
        check(not budget_destination.exists(), "byte-budget rejection created its requested output root")
    finally:
        exporter.MAX_OUTPUT_BYTES = original_budget
    print(f"engine control kit checks passed; retained artifacts at {args.output_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
