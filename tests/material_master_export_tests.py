#!/usr/bin/env python3
"""Standalone retained-artifact tests for export_material_master.py."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def check(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def canonical_sky_triangles(master: dict) -> list[dict]:
    output = []
    for row in master.get("sky_shadow_triangles", []):
        vertices = [[struct.unpack("<f", struct.pack("<f", float(axis)))[0] for axis in vertex]
                    for vertex in row["vertices"]]
        output.append({"id": row["id"], "vertices": vertices, "source_mask": row["source_mask"]})
    return output


def write_new(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)


def run_export(python: str, exporter: Path, master: Path, output: Path, expect_success: bool,
               expected_diagnostic: str | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([python, str(exporter), str(master), str(output)], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
                            env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"))
    check((result.returncode == 0) == expect_success,
          f"unexpected exporter status {result.returncode}: {result.stdout}{result.stderr}")
    if expected_diagnostic is not None:
        expected_stderr = f"material master export failed: {expected_diagnostic}\n"
        check(result.returncode == 2, f"rejection status changed for: {expected_diagnostic}")
        check(result.stderr == expected_stderr,
              f"diagnostic changed: expected {expected_stderr!r}, received {result.stderr!r}")
    return result


def all_files(root: Path) -> dict[str, bytes]:
    return {path.relative_to(root).as_posix(): path.read_bytes()
            for path in sorted(root.rglob("*")) if path.is_file()}


def canonical_manifest_hash(rows: list[dict]) -> str:
    text = ""
    for row in sorted(rows, key=lambda item: item["id"]):
        for field in ("id", "path", "kind", "sha256", "license", "source", "author", "notice"):
            value = row[field]
            text += f"{len(value.encode('utf-8'))}:{value}"
        text += "\n"
    return digest(text.encode("utf-8"))


def decode_oct(x_byte: int, y_byte: int) -> tuple[float, float, float]:
    x, y = x_byte / 255.0 * 2.0 - 1.0, y_byte / 255.0 * 2.0 - 1.0
    z = 1.0 - abs(x) - abs(y)
    if z < 0.0:
        old_x = x
        x = (1.0 - abs(y)) * (1.0 if old_x >= 0.0 else -1.0)
        y = (1.0 - abs(old_x)) * (1.0 if y >= 0.0 else -1.0)
    length = math.sqrt(x * x + y * y + z * z)
    return x / length, y / length, z / length


def reconstruct(pixel: list[float], encoded_depth: int, calibration: dict) -> tuple[float, float, float]:
    eppp = calibration["export_pixels_per_reference_pixel"]
    pivot = calibration["pivot_px"]
    u = (pixel[0] - pivot[0]) / eppp
    v = (pixel[1] - pivot[1]) / eppp
    depth_min, depth_max = calibration["depth_range"]
    depth = depth_min + encoded_depth / 65535.0 * (depth_max - depth_min)
    total = depth * math.sqrt(3.0)
    difference = u / 29.0
    half_sum_minus_z = v / 29.0
    coordinate_sum = (half_sum_minus_z + total) / 1.5
    z = total - coordinate_sum
    return (coordinate_sum + difference) / 2.0, (coordinate_sum - difference) / 2.0, z


def validate_export(root: Path, master: Path, exporter: Path) -> None:
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    check(manifest["schema"] == 1, "manifest schema changed")
    check(manifest["content_sha256"] == canonical_manifest_hash(manifest["assets"]),
          "manifest canonical content hash is wrong")
    for row in manifest["assets"]:
        data = (root / row["path"]).read_bytes()
        check(len(data) == row["bytes"] and digest(data) == row["sha256"],
              f"manifest byte ledger mismatch for {row['id']}")
        check(row["notice"] == "NOTICE.txt" and row["source"] == master.name,
              "manifest provenance row is incomplete")
    world = json.loads((root / "art/world.json").read_text(encoding="utf-8"))
    calibration = json.loads((root / "art/material/calibration.json").read_text(encoding="utf-8"))
    source_master = json.loads(master.read_text(encoding="utf-8"))
    expected_sky_triangles = canonical_sky_triangles(source_master)
    check(world["schema"] == 2 and world["pages"] == [], "world is not a material-only schema-2 catalog")
    check(len(world["textures"]) == 6 and len(world["materials"]) == 2 and len(world["lit_sprites"]) == 2,
          "schema-2 descriptor cardinality changed")
    check(calibration["master_sha256"] == digest(master.read_bytes()), "master hash is not the source byte hash")
    check(calibration["exporter_sha256"] == digest(exporter.read_bytes()), "exporter hash is not its source byte hash")
    check(len(calibration["camera_sha256"]) == 64 and len(calibration["settings_sha256"]) == 64,
          "camera/settings hashes are absent")
    check(calibration["export_pixels_per_reference_pixel"] == 4, "starting ePPP is not preserved")
    check(len(calibration["probes"]) >= 12 and
          all(probe["boundary_clearance_px"] >= 8.0 for probe in calibration["probes"]),
          "calibration probes are not safely interior")
    materials = {row["id"]: row for row in world["materials"]}
    off, on = materials[calibration["material_id_off"]], materials[calibration["material_id_on"]]
    check(off["channels"] == on["channels"] and off["source_hash"] == on["source_hash"],
          "emission variants do not share immutable source channels")
    check(off["emission"] == [0.0, 0.0, 0.0, 0.0] and on["emission"] == calibration["emission_radiance"],
          "off/on emission vectors are wrong")
    width, height = calibration["source_size"]
    check([width, height] == world["lit_sprites"][0]["source_size"], "sprite extent differs from calibration")
    expected_occluder = {"shape": "obb", "center_xy": [1.0, 1.0],
                         "axis_x": [1.0, 0.0], "axis_y": [0.0, 1.0],
                         "half_extent_x": 1.0, "half_extent_y": 1.0,
                         "base_z": 0.0, "top_z": struct.unpack("<f", struct.pack("<f", 2.4))[0]}
    check(all(sprite.get("local_occluders") == [expected_occluder]
              for sprite in world["lit_sprites"]),
          "authored calibration-cabin OBB was not exported deterministically")
    check(all(sprite.get("sky_shadow_triangles") == expected_sky_triangles
              for sprite in world["lit_sprites"]),
          "authored sky shadow triangles were not exported canonically into both descriptors")
    check(calibration.get("sky_shadow_triangle_count") == len(expected_sky_triangles),
          "authored sky shadow triangle count is absent from calibration metadata")
    check(calibration.get("sky_shadow_triangles_sha256") == digest(canonical_json(expected_sky_triangles)),
          "authored sky shadow descriptor hash is absent or incorrect")
    check({row["source_mask"] for row in expected_sky_triangles} == {1, 2, 3},
          "diagnostic master does not independently cover sun, moon, and shared source masks")
    texture_by_id = {row["id"]: row for row in world["textures"]}
    channels = off["channels"]
    depth_bytes = (root / texture_by_id[channels["camera_depth"]["texture"]]["path"]).read_bytes()
    normal_bytes = (root / texture_by_id[channels["normal_material"]["texture"]]["path"]).read_bytes()
    albedo_bytes = (root / texture_by_id[channels["albedo"]["texture"]]["path"]).read_bytes()
    emission_bytes = (root / texture_by_id[channels["emission_mask"]["texture"]]["path"]).read_bytes()
    for probe in calibration["probes"]:
        x, y = int(probe["source_pixel"][0]), int(probe["source_pixel"][1])
        pixel = y * width + x
        encoded_depth = struct.unpack_from("<H", depth_bytes, pixel * 2)[0]
        reconstructed = reconstruct(probe["source_pixel"], encoded_depth, calibration)
        check(max(abs(reconstructed[i] - probe["position"][i]) for i in range(3)) <= 0.02,
              f"XYZ reconstruction drifted at {probe['id']}")
        decoded_normal = decode_oct(normal_bytes[pixel * 4], normal_bytes[pixel * 4 + 1])
        check(max(abs(decoded_normal[i] - probe["normal"][i]) for i in range(3)) <= 0.02,
              f"oct normal reconstruction drifted at {probe['id']}")
        check(len(probe["triangle_vertices"]) == 3, "probe omitted source plane geometry")
    facade = next(probe for probe in calibration["probes"] if probe["id"] == "facade_x_1")
    window = next(probe for probe in calibration["probes"] if probe["id"] == "window_1")
    for probe, expected_mask in ((facade, 0), (window, 255)):
        x, y = int(probe["source_pixel"][0]), int(probe["source_pixel"][1])
        pixel = y * width + x
        check(emission_bytes[pixel] == expected_mask, "emission mask diagnostic changed")
    fx, fy = int(facade["source_pixel"][0]), int(facade["source_pixel"][1])
    wx, wy = int(window["source_pixel"][0]), int(window["source_pixel"][1])
    check(albedo_bytes[(fy * width + fx) * 4:(fy * width + fx + 1) * 4] ==
          albedo_bytes[(wy * width + wx) * 4:(wy * width + wx + 1) * 4],
          "window emission was baked into albedo RGB")


def write_mutation(path: Path, master: dict) -> None:
    write_new(path, (json.dumps(master, indent=2, allow_nan=True) + "\n").encode("utf-8"))


def snapped_size(master: dict, eppp: float) -> list[int]:
    projected = []
    for triangle in master["triangles"]:
        for x, y, z in triangle["vertices"]:
            projected.append((29.0 * (x - y), 14.5 * (x + y) - 29.0 * z))
    padding = master["export"]["padding_export_pixels"]
    minimum_u = math.floor(min(point[0] for point in projected) * eppp) - padding
    maximum_u = math.ceil(max(point[0] for point in projected) * eppp) + padding
    minimum_v = math.floor(min(point[1] for point in projected) * eppp) - padding
    maximum_v = math.ceil(max(point[1] for point in projected) * eppp) + padding
    return [maximum_u - minimum_u, maximum_v - minimum_v]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    destinations = parser.add_mutually_exclusive_group(required=True)
    destinations.add_argument("--output", type=Path, help="new retained artifact directory")
    destinations.add_argument("--output-parent", type=Path,
                              help="parent for a unique retained artifact directory")
    arguments = parser.parse_args()
    if arguments.output is not None:
        output = arguments.output.resolve()
        check(not output.exists(), "--output must name a directory that does not exist")
        output.mkdir(parents=True)
    else:
        parent = arguments.output_parent.resolve()
        parent.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="material-master-", dir=parent))
    repo = Path(__file__).resolve().parents[1]
    exporter = repo / "tools/export_material_master.py"
    master_path = repo / "art-source/engine-calibration/material-master.json"
    export_a, export_b = output / "export-a", output / "export-b"
    run_export(sys.executable, exporter, master_path, export_a, True)
    run_export(sys.executable, exporter, master_path, export_b, True)
    files_a, files_b = all_files(export_a), all_files(export_b)
    check(files_a.keys() == files_b.keys(), "repeat export file set differs")
    check(all(files_a[name] == files_b[name] for name in files_a), "repeat export bytes differ")
    validate_export(export_a, master_path, exporter)
    source = json.loads(master_path.read_text(encoding="utf-8"))

    omitted_source = json.loads(json.dumps(source))
    omitted_source.pop("sky_shadow_triangles")
    omitted_master = output / "sky-omitted-master.json"
    write_mutation(omitted_master, omitted_source)
    omitted_export = output / "sky-omitted-export"
    run_export(sys.executable, exporter, omitted_master, omitted_export, True)
    omitted_world = json.loads((omitted_export / "art/world.json").read_text(encoding="utf-8"))
    omitted_calibration = json.loads(
        (omitted_export / "art/material/calibration.json").read_text(encoding="utf-8"))
    check(all("sky_shadow_triangles" in sprite and sprite["sky_shadow_triangles"] == []
              for sprite in omitted_world["lit_sprites"]),
          "omitted master field did not canonically export an empty array in both descriptors")
    check(omitted_calibration["sky_shadow_triangle_count"] == 0 and
          omitted_calibration["sky_shadow_triangles_sha256"] == digest(canonical_json([])),
          "empty authored sky descriptor provenance is incorrect")

    utf8_boundary_source = json.loads(json.dumps(source))
    utf8_boundary_source["sky_shadow_triangles"][0]["id"] = "é" * 128
    utf8_boundary_master = output / "sky-utf8-256-master.json"
    write_mutation(utf8_boundary_master, utf8_boundary_source)
    utf8_boundary_export = output / "sky-utf8-256-export"
    run_export(sys.executable, exporter, utf8_boundary_master, utf8_boundary_export, True)
    utf8_boundary_world = json.loads(
        (utf8_boundary_export / "art/world.json").read_text(encoding="utf-8"))
    check(all(sprite["sky_shadow_triangles"][0]["id"] == "é" * 128
              for sprite in utf8_boundary_world["lit_sprites"]),
          "exactly 256 UTF-8 bytes were not accepted and preserved")

    caster_mask_source = json.loads(json.dumps(source))
    for triangle in caster_mask_source["triangles"]:
        triangle["caster_mask"] = 0.0
    caster_mask_master = output / "raster-caster-mask-zero-master.json"
    write_mutation(caster_mask_master, caster_mask_source)
    caster_mask_export = output / "raster-caster-mask-zero-export"
    run_export(sys.executable, exporter, caster_mask_master, caster_mask_export, True)
    baseline_world = json.loads((export_a / "art/world.json").read_text(encoding="utf-8"))
    caster_mask_world = json.loads((caster_mask_export / "art/world.json").read_text(encoding="utf-8"))
    baseline_calibration = json.loads(
        (export_a / "art/material/calibration.json").read_text(encoding="utf-8"))
    caster_mask_calibration = json.loads(
        (caster_mask_export / "art/material/calibration.json").read_text(encoding="utf-8"))
    check([row["sky_shadow_triangles"] for row in baseline_world["lit_sprites"]] ==
          [row["sky_shadow_triangles"] for row in caster_mask_world["lit_sprites"]] and
          baseline_calibration["sky_shadow_triangles_sha256"] ==
          caster_mask_calibration["sky_shadow_triangles_sha256"],
          "raster caster_mask changed independent authored sky geometry or its metadata hash")

    capsule_source = json.loads(json.dumps(source))
    capsule_source["local_occluders"] = [{"shape":"capsule","center_xy":[1,1],
        "axis_x":[0.6,0.8],"axis_y":[0,0],"half_length":0,"radius":0.25,"base_z":0,"top_z":2.4}]
    capsule_master = output / "capsule-master.json"
    write_new(capsule_master, (json.dumps(capsule_source)+"\n").encode("utf-8"))
    run_export(sys.executable, exporter, capsule_master, output / "capsule-export", True)
    capsule_world = json.loads((output / "capsule-export/art/world.json").read_text(encoding="utf-8"))
    canonical_capsule = json.loads(json.dumps(capsule_source["local_occluders"]))
    for row in canonical_capsule:
        for key, value in row.items():
            if isinstance(value, list):
                row[key] = [struct.unpack("<f", struct.pack("<f", part))[0] for part in value]
            elif isinstance(value, (int, float)):
                row[key] = struct.unpack("<f", struct.pack("<f", value))[0]
    check(all(sprite["local_occluders"] == canonical_capsule for sprite in capsule_world["lit_sprites"]),
          "Capsule reserved-zero axis / zero half length did not survive export")
    negative_root = output / "negative-fixtures"
    divergence = json.loads(json.dumps(source))
    raw_divergent_eppp = 0.005683971201213582
    divergence["export"]["export_pixels_per_reference_pixel"] = raw_divergent_eppp
    for triangle in divergence["triangles"]:
        expanded = []
        for x, y, z in triangle["vertices"]:
            projected_u = 29.0 * (x - y) * 650.0
            projected_v = (14.5 * (x + y) - 29.0 * z) * 650.0
            depth = (x + y + z) / math.sqrt(3.0)
            difference = projected_u / 29.0
            projected_sum_minus_two_z = projected_v / 14.5
            sum_plus_z = depth * math.sqrt(3.0)
            new_z = (sum_plus_z - projected_sum_minus_two_z) / 3.0
            coordinate_sum = (2.0 * sum_plus_z + projected_sum_minus_two_z) / 3.0
            expanded.append([(coordinate_sum + difference) * 0.5,
                             (coordinate_sum - difference) * 0.5, new_z])
        triangle["vertices"] = expanded
    divergence_path = output / "float32-eppp-divergence.json"
    write_mutation(divergence_path, divergence)
    divergence_output = output / "float32-eppp-export"
    run_export(sys.executable, exporter, divergence_path, divergence_output, True)
    canonical_eppp = struct.unpack("<f", struct.pack("<f", raw_divergent_eppp))[0]
    raw_size, canonical_size = snapped_size(divergence, raw_divergent_eppp), snapped_size(divergence, canonical_eppp)
    divergence_calibration = json.loads(
        (divergence_output / "art/material/calibration.json").read_text(encoding="utf-8"))
    check(raw_size != canonical_size, "ePPP mutation no longer straddles a projection snapping boundary")
    check(divergence_calibration["source_size"] == canonical_size,
          "export bounds did not use the runtime float32 ePPP")
    check(divergence_calibration["export_pixels_per_reference_pixel"] == canonical_eppp and
          divergence_calibration["master_export_inputs"]["export_pixels_per_reference_pixel"] == raw_divergent_eppp,
          "canonical/runtime and raw/source ePPP values were not kept distinct")
    cases: list[tuple[str, dict, str]] = []
    invalid_shape = json.loads(json.dumps(source)); invalid_shape["local_occluders"][0]["shape"] = "mesh"
    cases.append(("occluder-shape", invalid_shape,
                  "local_occluders[0].shape must be 'obb' or 'capsule'"))
    invalid_axes = json.loads(json.dumps(source)); invalid_axes["local_occluders"][0]["axis_y"] = [1, 0]
    cases.append(("occluder-axes", invalid_axes,
                  "local_occluders[0] axes must be finite orthonormal unit vectors"))
    invalid_base = json.loads(json.dumps(source)); invalid_base["local_occluders"][0]["base_z"] = 2.4
    cases.append(("occluder-base", invalid_base,
                  "local_occluders[0] height must be at least 0.0001"))
    tiny_height = json.loads(json.dumps(source)); tiny_height["local_occluders"][0]["top_z"] = 1e-6
    cases.append(("occluder-tiny-height", tiny_height, "local_occluders[0] height must be at least 0.0001"))
    collapsed_height = json.loads(json.dumps(source))
    collapsed_height["local_occluders"][0].update(base_z=4096.0, top_z=4096.0001)
    cases.append(("occluder-float32-height-collapse", collapsed_height, "local_occluders[0] height must be at least 0.0001"))
    tiny_extent = json.loads(json.dumps(source)); tiny_extent["local_occluders"][0]["half_extent_x"] = 1e-6
    cases.append(("occluder-tiny-extent", tiny_extent, "local_occluders[0] OBB half extents must be at least 0.0001"))
    tiny_radius = json.loads(json.dumps(capsule_source)); tiny_radius["local_occluders"][0]["radius"] = 1e-6
    cases.append(("occluder-tiny-radius", tiny_radius, "local_occluders[0] capsule radius must be at least 0.0001"))
    invalid_occluder_nan = json.loads(json.dumps(source)); invalid_occluder_nan["local_occluders"][0]["center_xy"][0] = float("nan")
    cases.append(("occluder-nan", invalid_occluder_nan,
                  "local_occluders[0].center_xy[0] must be a finite number"))
    sky_non_array = json.loads(json.dumps(source)); sky_non_array["sky_shadow_triangles"] = {}
    cases.append(("sky-non-array", sky_non_array,
                  "sky_shadow_triangles must be an array of at most 4096 triangles"))
    sky_null = json.loads(json.dumps(source)); sky_null["sky_shadow_triangles"] = None
    cases.append(("sky-null", sky_null,
                  "sky_shadow_triangles must be an array of at most 4096 triangles"))
    sky_unknown_field = json.loads(json.dumps(source)); sky_unknown_field["sky_shadow_triangles"][0]["reveal"] = 1
    cases.append(("sky-unknown-field", sky_unknown_field,
                  "sky_shadow_triangles[0] fields must be exactly id, vertices, and source_mask"))
    sky_missing_field = json.loads(json.dumps(source)); sky_missing_field["sky_shadow_triangles"][0].pop("source_mask")
    cases.append(("sky-missing-field", sky_missing_field,
                  "sky_shadow_triangles[0] fields must be exactly id, vertices, and source_mask"))
    sky_wrong_vertices = json.loads(json.dumps(source)); sky_wrong_vertices["sky_shadow_triangles"][0]["vertices"].pop()
    cases.append(("sky-wrong-vertices", sky_wrong_vertices,
                  "sky_shadow_triangles[0].vertices must contain exactly three vertices"))
    sky_wrong_vertex = json.loads(json.dumps(source)); sky_wrong_vertex["sky_shadow_triangles"][0]["vertices"][0].pop()
    cases.append(("sky-wrong-vertex", sky_wrong_vertex,
                  "sky_shadow_triangles[0].vertices[0] must contain 3 numbers"))
    sky_nonfinite = json.loads(json.dumps(source)); sky_nonfinite["sky_shadow_triangles"][0]["vertices"][0][0] = float("nan")
    cases.append(("sky-nonfinite", sky_nonfinite,
                  "sky_shadow_triangles[0].vertices[0][0] must be a finite number"))
    sky_out_of_range = json.loads(json.dumps(source)); sky_out_of_range["sky_shadow_triangles"][0]["vertices"][0][0] = 8193
    cases.append(("sky-out-of-range", sky_out_of_range,
                  "sky_shadow_triangles[0].vertices[0][0] exceeds the authored [-8192, 8192] coordinate contract"))
    sky_duplicate = json.loads(json.dumps(source)); sky_duplicate["sky_shadow_triangles"][1]["id"] = sky_duplicate["sky_shadow_triangles"][0]["id"]
    cases.append(("sky-duplicate-id", sky_duplicate,
                  f"duplicate sky shadow triangle id: {sky_duplicate['sky_shadow_triangles'][0]['id']}"))
    sky_degenerate = json.loads(json.dumps(source)); sky_degenerate["sky_shadow_triangles"][0]["vertices"][2] = sky_degenerate["sky_shadow_triangles"][0]["vertices"][1]
    cases.append(("sky-degenerate", sky_degenerate,
                  "sky_shadow_triangles[0] is degenerate after float32 canonicalization"))
    sky_float32_collapse = json.loads(json.dumps(source))
    sky_float32_collapse["sky_shadow_triangles"][0]["vertices"] = [
        [4096.0, 0.0, 0.0], [4096.0001, 0.0, 0.0], [4096.0, 0.0001, 0.0]]
    cases.append(("sky-float32-collapse", sky_float32_collapse,
                  "sky_shadow_triangles[0] is degenerate after float32 canonicalization"))
    sky_float_mask = json.loads(json.dumps(source)); sky_float_mask["sky_shadow_triangles"][0]["source_mask"] = 1.0
    cases.append(("sky-float-mask", sky_float_mask,
                  "sky_shadow_triangles[0].source_mask must be the integer 1, 2, or 3"))
    sky_bool_mask = json.loads(json.dumps(source)); sky_bool_mask["sky_shadow_triangles"][0]["source_mask"] = True
    cases.append(("sky-bool-mask", sky_bool_mask,
                  "sky_shadow_triangles[0].source_mask must be the integer 1, 2, or 3"))
    sky_unknown_mask = json.loads(json.dumps(source)); sky_unknown_mask["sky_shadow_triangles"][0]["source_mask"] = 4
    cases.append(("sky-unknown-mask", sky_unknown_mask,
                  "sky_shadow_triangles[0].source_mask must be the integer 1, 2, or 3"))
    sky_long_id = json.loads(json.dumps(source)); sky_long_id["sky_shadow_triangles"][0]["id"] = "é" * 129
    cases.append(("sky-long-utf8-id", sky_long_id,
                  "sky_shadow_triangles[0].id must be a non-empty UTF-8 string of at most 256 bytes"))
    sky_empty_id = json.loads(json.dumps(source)); sky_empty_id["sky_shadow_triangles"][0]["id"] = ""
    cases.append(("sky-empty-id", sky_empty_id,
                  "sky_shadow_triangles[0].id must be a non-empty UTF-8 string of at most 256 bytes"))
    sky_raw_overflow = json.loads(json.dumps(source))
    sky_template_vertices = source["sky_shadow_triangles"][0]["vertices"]
    sky_raw_overflow["sky_shadow_triangles"] = [
        {"id": f"raw.{index}", "vertices": sky_template_vertices, "source_mask": 1}
        for index in range(4097)]
    cases.append(("sky-raw-overflow", sky_raw_overflow,
                  "sky_shadow_triangles must be an array of at most 4096 triangles"))
    sky_sun_overflow = json.loads(json.dumps(source))
    sky_sun_overflow["sky_shadow_triangles"] = [
        {"id": f"sun.{index}", "vertices": sky_template_vertices, "source_mask": 1}
        for index in range(1025)]
    cases.append(("sky-sun-overflow", sky_sun_overflow,
                  "sky_shadow_triangles exceeds 1024 active triangles for a source"))
    sky_moon_overflow = json.loads(json.dumps(source))
    sky_moon_overflow["sky_shadow_triangles"] = [
        {"id": f"moon.{index}", "vertices": sky_template_vertices, "source_mask": 2}
        for index in range(1025)]
    cases.append(("sky-moon-overflow", sky_moon_overflow,
                  "sky_shadow_triangles exceeds 1024 active triangles for a source"))
    nonfinite = json.loads(json.dumps(source)); nonfinite["triangles"][0]["vertices"][0][0] = float("nan")
    cases.append(("nonfinite", nonfinite, "triangles[0].vertices[0] must be a finite number"))
    huge_number = json.loads(json.dumps(source)); huge_number["triangles"][0]["vertices"][0][0] = 10 ** 400
    cases.append(("huge-number", huge_number, "triangles[0].vertices[0] must be a finite number"))
    huge_coordinate = json.loads(json.dumps(source)); huge_coordinate["triangles"][0]["vertices"][0][0] = 1e308
    cases.append(("huge-coordinate", huge_coordinate,
                  "triangles[0] vertex exceeds GameArt [-8192, 8192] contract"))
    degenerate = json.loads(json.dumps(source)); degenerate["triangles"][0]["vertices"][2] = degenerate["triangles"][0]["vertices"][1]
    cases.append(("degenerate", degenerate, "triangles[0] is degenerate"))
    coordinate = json.loads(json.dumps(source)); coordinate["coordinate_system"] = "unsupported-left-handed"
    cases.append(("coordinate", coordinate, "unsupported coordinate_system"))
    oversized = json.loads(json.dumps(source)); oversized["export"]["export_pixels_per_reference_pixel"] = 64
    cases.append(("oversized", oversized,
                  "snapped output 14122x10874 exceeds 2048 dimension/4194304 texel budget"))
    eppp_zero = json.loads(json.dumps(source)); eppp_zero["export"]["export_pixels_per_reference_pixel"] = 0
    cases.append(("eppp-zero", eppp_zero,
                  "export_pixels_per_reference_pixel is outside [0.001, 64]"))
    unknown_triangle = json.loads(json.dumps(source)); unknown_triangle["probes"][0]["triangle_id"] = "missing.triangle"
    cases.append(("unknown-triangle", unknown_triangle,
                  "probes[0] has a duplicate ID or unknown triangle"))
    probe_outside = json.loads(json.dumps(source)); probe_outside["probes"][0]["barycentric"] = [-0.1, 0.5, 0.6]
    cases.append(("probe-outside", probe_outside,
                  "probes[0].barycentric must be positive and sum to one"))
    depth_domain = json.loads(json.dumps(source))
    for triangle in depth_domain["triangles"]:
        triangle["vertices"] = [[axis + 5000 for axis in vertex] for vertex in triangle["vertices"]]
    cases.append(("depth-domain", depth_domain,
                  "camera depth range exceeds GameArt [-8192, 8192] contract"))
    pivot_domain = json.loads(json.dumps(source))
    for triangle in pivot_domain["triangles"]:
        triangle["vertices"] = [[vertex[0] + 10, vertex[1] + 10, vertex[2]]
                                for vertex in triangle["vertices"]]
    cases.append(("pivot-domain", pivot_domain, "snapped pivot is outside source extent"))
    lateral_domain = json.loads(json.dumps(source))
    lateral_domain["export"]["export_pixels_per_reference_pixel"] = 0.001
    for triangle in lateral_domain["triangles"]:
        expanded = []
        for x, y, z in triangle["vertices"]:
            lateral = 1425.0 * (x - y)
            expanded.append([x + lateral, y - lateral, z])
        triangle["vertices"] = expanded
    cases.append(("lateral-domain", lateral_domain,
                  "reconstructed source/depth corner exceeds renderer [-8192, 8192] contract"))
    narrow_depth = json.loads(json.dumps(source))
    target_depth = 4096.0 + 0.0001220703125
    for triangle in narrow_depth["triangles"]:
        compressed = []
        for x, y, z in triangle["vertices"]:
            projected_u = 29.0 * (x - y)
            projected_v = 14.5 * (x + y) - 29.0 * z
            new_depth = target_depth + (x + y + z) / math.sqrt(3.0) * 0.00001
            difference = projected_u / 29.0
            projected_sum_minus_two_z = projected_v / 14.5
            sum_plus_z = new_depth * math.sqrt(3.0)
            new_z = (sum_plus_z - projected_sum_minus_two_z) / 3.0
            coordinate_sum = (2.0 * sum_plus_z + projected_sum_minus_two_z) / 3.0
            compressed.append([(coordinate_sum + difference) * 0.5,
                               (coordinate_sum - difference) * 0.5, new_z])
        triangle["vertices"] = compressed
    cases.append(("float32-depth-collapse", narrow_depth,
                  "camera depth range collapses in float32"))
    diagnostic_results = []
    for name, fixture, expected_diagnostic in cases:
        fixture_path = negative_root / f"{name}.json"
        write_mutation(fixture_path, fixture)
        rejected_output = output / f"rejected-{name}"
        result = run_export(sys.executable, exporter, fixture_path, rejected_output, False,
                            expected_diagnostic)
        check(not rejected_output.exists(), f"rejected {name} left a partial output directory")
        diagnostic_results.append({"case": name, "status": result.returncode,
                                   "diagnostic": expected_diagnostic})
    existing = output / "existing-output"
    existing.mkdir()
    sentinel = b"preserve-existing-output\n"
    write_new(existing / "sentinel.txt", sentinel)
    existing_diagnostic = "output root already exists; exporter refuses to overwrite"
    existing_result = run_export(sys.executable, exporter, master_path, existing, False,
                                 existing_diagnostic)
    check((existing / "sentinel.txt").read_bytes() == sentinel and len(list(existing.iterdir())) == 1,
          "existing output was modified")
    diagnostic_results.append({"case": "existing-output", "status": existing_result.returncode,
                               "diagnostic": existing_diagnostic})
    report = {"schema": 1, "result": "pass", "deterministic_file_count": len(files_a),
              "probes": len(json.loads((export_a / "art/material/calibration.json").read_text(encoding="utf-8"))["probes"]),
              "negative_cases": [name for name, _, _ in cases] + ["existing-output"],
              "zero_half_length_reserved_axis_capsule_export": True,
              "authored_sky_shadow": {
                  "triangle_count": len(source["sky_shadow_triangles"]),
                  "source_masks": sorted({row["source_mask"] for row in source["sky_shadow_triangles"]}),
                  "canonical_empty_export": True,
                  "utf8_256_byte_id": True,
                  "raster_caster_mask_independent": True,
                  "descriptor_sha256": json.loads(
                      (export_a / "art/material/calibration.json").read_text(encoding="utf-8"))[
                          "sky_shadow_triangles_sha256"]},
              "source_sha256": {
                  "master": digest(master_path.read_bytes()),
                  "exporter": digest(exporter.read_bytes()),
                  "test": digest(Path(__file__).read_bytes())},
              "diagnostic_results": diagnostic_results,
              "float32_eppp_divergence": {"raw": raw_divergent_eppp,
                                           "canonical": canonical_eppp,
                                           "raw_source_size": raw_size,
                                           "canonical_source_size": canonical_size},
              "artifacts_retained": output.as_posix()}
    write_new(output / "test-report.json", (json.dumps(report, indent=2) + "\n").encode("utf-8"))
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
