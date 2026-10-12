#!/usr/bin/env python3
"""Deterministically rasterize an editable Ribterra geometry material master."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import os
import re
import shutil
import stat
import struct
import sys
from pathlib import Path, PurePosixPath


MAX_DIMENSION = 2048
MAX_TEXELS = 4_194_304
SQRT3 = math.sqrt(3.0)
CHANNELS = (
    ("albedo", "albedo.rtex", "rgba8_unorm_srgb", 4),
    ("normal_material", "normal-material.rtex", "rgba8_unorm", 4),
    ("camera_depth", "camera-depth.rtex", "r16_unorm", 2),
    ("emission_mask", "emission-mask.rtex", "r8_unorm", 1),
    ("surface_mask", "surface-mask.rtex", "rgba8_unorm", 4),
    ("caster_mask", "caster-mask.rtex", "r8_unorm", 1),
)
MAX_COLLECTION_BYTES = 128 * 1024 * 1024
MIN_COLLECTION_FREE_BYTES = 384 * 1024 * 1024
AUTHORED_ALBEDO_KIND = "authored-xyz-triangle-materials-v1"
MAX_PALETTE_BYTES = 512 * 1024
MAX_AUTHORED_NOTICE_BYTES = 128 * 1024


class ExportError(ValueError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def float32(value: float) -> float:
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def adjacent_float32(value: float, positive: bool) -> float:
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    if positive:
        bits = bits + 1 if value >= 0.0 else bits - 1
    else:
        bits = bits - 1 if value > 0.0 else (0x80000001 if value == 0.0 else bits + 1)
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def finite_number(value: object, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ExportError(f"{label} must be a finite number")
    try:
        converted = float(value)
    except (OverflowError, ValueError):
        raise ExportError(f"{label} must be a finite number") from None
    if not math.isfinite(converted):
        raise ExportError(f"{label} must be a finite number")
    return converted


def vector(value: object, count: int, label: str) -> tuple[float, ...]:
    if not isinstance(value, list) or len(value) != count:
        raise ExportError(f"{label} must contain {count} numbers")
    return tuple(finite_number(item, f"{label}[{index}]") for index, item in enumerate(value))


def byte_vector(value: object, count: int, label: str) -> tuple[int, ...]:
    values = vector(value, count, label)
    if any(item != round(item) or item < 0 or item > 255 for item in values):
        raise ExportError(f"{label} must contain byte integers")
    return tuple(int(item) for item in values)


def identifier(value: object, label: str) -> str:
    if not isinstance(value, str) or not value or len(value) > 256:
        raise ExportError(f"{label} must be a non-empty string of at most 256 characters")
    return value


def parse_sky_shadow_triangles(value: object) -> list[dict]:
    """Validate independent authored directional-shadow geometry."""
    if not isinstance(value, list) or len(value) > 4096:
        raise ExportError("sky_shadow_triangles must be an array of at most 4096 triangles")
    output = []
    triangle_ids = set()
    active_counts = [0, 0]

    def canonical_coordinate(item: object, label: str) -> float:
        coordinate = finite_number(float32(finite_number(item, label)), label)
        if abs(coordinate) > 8192.0:
            raise ExportError(f"{label} exceeds the authored [-8192, 8192] coordinate contract")
        return coordinate

    for index, row in enumerate(value):
        label = f"sky_shadow_triangles[{index}]"
        if not isinstance(row, dict):
            raise ExportError(f"{label} must be an object")
        if set(row) != {"id", "vertices", "source_mask"}:
            raise ExportError(f"{label} fields must be exactly id, vertices, and source_mask")
        triangle_id = row.get("id")
        if not isinstance(triangle_id, str) or not triangle_id:
            raise ExportError(f"{label}.id must be a non-empty UTF-8 string of at most 256 bytes")
        try:
            identifier_bytes = triangle_id.encode("utf-8")
        except UnicodeEncodeError:
            raise ExportError(f"{label}.id must be a non-empty UTF-8 string of at most 256 bytes") from None
        if len(identifier_bytes) > 256:
            raise ExportError(f"{label}.id must be a non-empty UTF-8 string of at most 256 bytes")
        if triangle_id in triangle_ids:
            raise ExportError(f"duplicate sky shadow triangle id: {triangle_id}")
        triangle_ids.add(triangle_id)
        vertices_value = row.get("vertices")
        if not isinstance(vertices_value, list) or len(vertices_value) != 3:
            raise ExportError(f"{label}.vertices must contain exactly three vertices")
        vertices = []
        for vertex_index, vertex_value in enumerate(vertices_value):
            vertex_label = f"{label}.vertices[{vertex_index}]"
            if not isinstance(vertex_value, list) or len(vertex_value) != 3:
                raise ExportError(f"{vertex_label} must contain 3 numbers")
            vertices.append([canonical_coordinate(axis, f"{vertex_label}[{axis_index}]")
                             for axis_index, axis in enumerate(vertex_value)])
        edge_a = subtract(tuple(vertices[1]), tuple(vertices[0]))
        edge_b = subtract(tuple(vertices[2]), tuple(vertices[0]))
        normal = cross(edge_a, edge_b)
        if sum(component * component for component in normal) == 0.0:
            raise ExportError(f"{label} is degenerate after float32 canonicalization")
        source_mask = row.get("source_mask")
        if isinstance(source_mask, bool) or not isinstance(source_mask, int) or source_mask not in (1, 2, 3):
            raise ExportError(f"{label}.source_mask must be the integer 1, 2, or 3")
        if source_mask & 1:
            active_counts[0] += 1
        if source_mask & 2:
            active_counts[1] += 1
        if active_counts[0] > 1024 or active_counts[1] > 1024:
            raise ExportError("sky_shadow_triangles exceeds 1024 active triangles for a source")
        output.append({"id": triangle_id, "vertices": vertices, "source_mask": source_mask})
    return output


def parse_local_occluders(value: object) -> list[dict]:
    """Validate authored vertical prisms; instance owner/stable IDs are runtime-only."""
    if value is None:
        return []
    if not isinstance(value, list) or len(value) > 32:
        raise ExportError("local_occluders must be an array of at most 32 primitives")
    output = []
    minimum_dimension = float32(1e-4)
    def canonical_number(item: object, label: str) -> float:
        return finite_number(float32(finite_number(item, label)), label)
    def canonical_vector(item: object, label: str) -> tuple[float, ...]:
        values = vector(item, 2, label)
        return tuple(canonical_number(part, f"{label}[{index}]") for index, part in enumerate(values))
    for index, row in enumerate(value):
        label = f"local_occluders[{index}]"
        if not isinstance(row, dict):
            raise ExportError(f"{label} must be an object")
        shape = row.get("shape")
        if shape not in ("obb", "capsule"):
            raise ExportError(f"{label}.shape must be 'obb' or 'capsule'")
        center = canonical_vector(row.get("center_xy"), f"{label}.center_xy")
        axis_x = canonical_vector(row.get("axis_x"), f"{label}.axis_x")
        axis_y = canonical_vector(row.get("axis_y"), f"{label}.axis_y")
        length_x = math.hypot(*axis_x)
        length_y = math.hypot(*axis_y)
        dot = axis_x[0] * axis_y[0] + axis_x[1] * axis_y[1]
        if abs(length_x - 1.0) > 1e-6 or (shape == "obb" and
                (abs(length_y - 1.0) > 1e-6 or abs(dot) > 1e-6)):
            raise ExportError(f"{label} axes must be finite orthonormal unit vectors")
        if shape == "capsule" and axis_y != (0.0, 0.0):
            raise ExportError(f"{label} capsule axis_y must be reserved zero")
        base_z = canonical_number(row.get("base_z"), f"{label}.base_z")
        top_z = canonical_number(row.get("top_z"), f"{label}.top_z")
        if any(abs(item) > 8192.0 for item in (*center, base_z, top_z)):
            raise ExportError(f"{label} exceeds the GameArt [-8192, 8192] geometry contract")
        if float32(top_z - base_z) < minimum_dimension:
            raise ExportError(f"{label} height must be at least 0.0001")
        primitive = {"shape": shape, "center_xy": list(center), "axis_x": list(axis_x),
                     "axis_y": list(axis_y), "base_z": base_z, "top_z": top_z}
        if shape == "obb":
            allowed = {"shape", "center_xy", "axis_x", "axis_y", "half_extent_x",
                       "half_extent_y", "base_z", "top_z"}
            extent_x = canonical_number(row.get("half_extent_x"), f"{label}.half_extent_x")
            extent_y = canonical_number(row.get("half_extent_y"), f"{label}.half_extent_y")
            if extent_x < minimum_dimension or extent_y < minimum_dimension:
                raise ExportError(f"{label} OBB half extents must be at least 0.0001")
            if extent_x > 8192.0 or extent_y > 8192.0:
                raise ExportError(f"{label} exceeds the GameArt [-8192, 8192] geometry contract")
            primitive.update(half_extent_x=extent_x, half_extent_y=extent_y)
        else:
            allowed = {"shape", "center_xy", "axis_x", "axis_y", "half_length", "radius",
                       "base_z", "top_z"}
            half_length = canonical_number(row.get("half_length"), f"{label}.half_length")
            radius = canonical_number(row.get("radius"), f"{label}.radius")
            if half_length < 0.0 or radius <= 0.0:
                raise ExportError(f"{label} capsule half length must be nonnegative and radius positive")
            if radius < minimum_dimension:
                raise ExportError(f"{label} capsule radius must be at least 0.0001")
            if half_length > 8192.0 or radius > 8192.0:
                raise ExportError(f"{label} exceeds the GameArt [-8192, 8192] geometry contract")
            primitive.update(half_length=half_length, radius=radius)
        if set(row) != allowed:
            raise ExportError(f"{label} fields do not match the {shape} contract")
        output.append(primitive)
    return output


def subtract(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, float, float]:
    return a[0] - b[0], a[1] - b[1], a[2] - b[2]


def cross(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, float, float]:
    return a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]


def normalize(value: tuple[float, ...], label: str) -> tuple[float, float, float]:
    length = math.sqrt(sum(item * item for item in value))
    if not math.isfinite(length) or length <= 1e-9:
        raise ExportError(f"{label} is degenerate")
    return value[0] / length, value[1] / length, value[2] / length


def projection(point: tuple[float, ...]) -> tuple[float, float]:
    return 29.0 * (point[0] - point[1]), 14.5 * (point[0] + point[1]) - 29.0 * point[2]


def camera_depth(point: tuple[float, ...]) -> float:
    return sum(point) / SQRT3


def reconstruct_xyz(projected: tuple[float, float], depth: float) -> tuple[float, float, float]:
    difference = projected[0] / 29.0
    projected_sum_minus_two_z = projected[1] / 14.5
    sum_plus_z = depth * SQRT3
    z = (sum_plus_z - projected_sum_minus_two_z) / 3.0
    coordinate_sum = (2.0 * sum_plus_z + projected_sum_minus_two_z) / 3.0
    return (coordinate_sum + difference) * 0.5, (coordinate_sum - difference) * 0.5, z


def barycentric(point: tuple[float, float], triangle: tuple[tuple[float, float], ...]) -> tuple[float, float, float] | None:
    a, b, c = triangle
    denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    if abs(denominator) <= 1e-12:
        return None
    w0 = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / denominator
    w1 = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / denominator
    return w0, w1, 1.0 - w0 - w1


def interpolate(weights: tuple[float, ...], vertices: tuple[tuple[float, ...], ...]) -> tuple[float, float, float]:
    return tuple(sum(weights[i] * vertices[i][axis] for i in range(3)) for axis in range(3))  # type: ignore[return-value]


def edge_distance(point: tuple[float, float], a: tuple[float, float], b: tuple[float, float]) -> float:
    numerator = abs((b[0] - a[0]) * (a[1] - point[1]) - (a[0] - point[0]) * (b[1] - a[1]))
    return numerator / math.hypot(b[0] - a[0], b[1] - a[1])


def oct_encode(normal: tuple[float, ...]) -> tuple[int, int]:
    x, y, z = normal
    scale = abs(x) + abs(y) + abs(z)
    x, y, z = x / scale, y / scale, z / scale
    if z < 0.0:
        old_x = x
        x = (1.0 - abs(y)) * (1.0 if old_x >= 0.0 else -1.0)
        y = (1.0 - abs(old_x)) * (1.0 if y >= 0.0 else -1.0)
    return round((x * 0.5 + 0.5) * 255.0), round((y * 0.5 + 0.5) * 255.0)


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def parse_master(raw: bytes) -> tuple[dict, dict]:
    try:
        master = json.loads(raw)
    except (UnicodeDecodeError, ValueError) as error:
        raise ExportError(f"master is not valid UTF-8 JSON: {error}") from error
    if not isinstance(master, dict) or master.get("schema") != 1:
        raise ExportError("master schema must be 1")
    if master.get("coordinate_system") != "ribterra-world-xyz-v1":
        raise ExportError("unsupported coordinate_system")
    settings = master.get("export")
    if not isinstance(settings, dict):
        raise ExportError("export must be an object")
    raw_eppp = finite_number(settings.get("export_pixels_per_reference_pixel"), "export_pixels_per_reference_pixel")
    eppp = float32(raw_eppp)
    padding = finite_number(settings.get("padding_export_pixels"), "padding_export_pixels")
    if eppp < 0.001 or eppp > 64.0:
        raise ExportError("export_pixels_per_reference_pixel is outside [0.001, 64]")
    if padding != round(padding) or padding < 0 or padding > 256:
        raise ExportError("padding_export_pixels must be an integer in [0, 256]")
    ids = master.get("ids")
    if not isinstance(ids, dict):
        raise ExportError("ids must be an object")
    parsed_ids = {name: identifier(ids.get(name), f"ids.{name}") for name in
                  ("material_off", "material_on", "sprite_off", "sprite_on")}
    if len(set(parsed_ids.values())) != 4:
        raise ExportError("material and sprite IDs must be unique")
    emission_radiance = vector(master.get("emission_radiance"), 4, "emission_radiance")
    if any(item < 0.0 or item > 64.0 for item in emission_radiance):
        raise ExportError("emission_radiance must be within [0, 64]")
    local_occluders = parse_local_occluders(master.get("local_occluders"))
    sky_shadow_triangles = (parse_sky_shadow_triangles(master["sky_shadow_triangles"])
                            if "sky_shadow_triangles" in master else [])
    triangles_value = master.get("triangles")
    if not isinstance(triangles_value, list) or not triangles_value:
        raise ExportError("triangles must be a non-empty array")
    if len(triangles_value) > 10000:
        raise ExportError("triangle count exceeds 10000")
    triangles = []
    triangle_ids = set()
    for index, row in enumerate(triangles_value):
        label = f"triangles[{index}]"
        if not isinstance(row, dict):
            raise ExportError(f"{label} must be an object")
        triangle_id = identifier(row.get("id"), f"{label}.id")
        if triangle_id in triangle_ids:
            raise ExportError(f"duplicate triangle id: {triangle_id}")
        triangle_ids.add(triangle_id)
        vertices_value = row.get("vertices")
        if not isinstance(vertices_value, list) or len(vertices_value) != 3:
            raise ExportError(f"{label}.vertices must contain exactly three vertices")
        vertices = tuple(vector(item, 3, f"{label}.vertices") for item in vertices_value)
        if any(abs(axis) > 8192.0 for vertex in vertices for axis in vertex):
            raise ExportError(f"{label} vertex exceeds GameArt [-8192, 8192] contract")
        normal = normalize(cross(subtract(vertices[1], vertices[0]), subtract(vertices[2], vertices[0])), label)
        projected = tuple(projection(point) for point in vertices)
        if barycentric(projected[0], projected) is None:
            raise ExportError(f"{label} is degenerate in the required camera projection")
        albedo = byte_vector(row.get("albedo"), 4, f"{label}.albedo")
        roughness = finite_number(row.get("roughness"), f"{label}.roughness")
        ao = finite_number(row.get("ao"), f"{label}.ao")
        emission = finite_number(row.get("emission_mask"), f"{label}.emission_mask")
        wet = finite_number(row.get("wet_mask"), f"{label}.wet_mask")
        caster = finite_number(row.get("caster_mask"), f"{label}.caster_mask")
        for name, value in (("roughness", roughness), ("ao", ao), ("emission_mask", emission),
                            ("wet_mask", wet), ("caster_mask", caster)):
            if value < 0.0 or value > 1.0:
                raise ExportError(f"{label}.{name} must be within [0, 1]")
        triangles.append({"id": triangle_id, "vertices": vertices, "projected": projected,
                          "normal": normal, "albedo": albedo, "roughness": roughness,
                          "ao": ao, "emission_mask": emission, "wet_mask": wet, "caster_mask": caster})
    probes_value = master.get("probes")
    if not isinstance(probes_value, list) or len(probes_value) < 12:
        raise ExportError("probes must contain at least twelve entries")
    probes = []
    probe_ids = set()
    for index, row in enumerate(probes_value):
        label = f"probes[{index}]"
        if not isinstance(row, dict):
            raise ExportError(f"{label} must be an object")
        probe_id = identifier(row.get("id"), f"{label}.id")
        triangle_id = identifier(row.get("triangle_id"), f"{label}.triangle_id")
        weights = vector(row.get("barycentric"), 3, f"{label}.barycentric")
        if probe_id in probe_ids or triangle_id not in triangle_ids:
            raise ExportError(f"{label} has a duplicate ID or unknown triangle")
        if any(item <= 0.0 for item in weights) or abs(sum(weights) - 1.0) > 1e-8:
            raise ExportError(f"{label}.barycentric must be positive and sum to one")
        probe_ids.add(probe_id)
        probes.append({"id": probe_id, "triangle_id": triangle_id, "barycentric": weights})
    return master, {"eppp": eppp, "padding": int(padding), "ids": parsed_ids,
                    "emission_radiance": emission_radiance, "triangles": triangles, "probes": probes,
                    "local_occluders": local_occluders,
                    "sky_shadow_triangles": sky_shadow_triangles}


def rasterize(parsed: dict) -> dict:
    eppp, padding, triangles = parsed["eppp"], parsed["padding"], parsed["triangles"]
    all_projected = [coordinate * eppp for triangle in triangles for point in triangle["projected"] for coordinate in point]
    us, vs = all_projected[0::2], all_projected[1::2]
    min_u, max_u = math.floor(min(us)) - padding, math.ceil(max(us)) + padding
    min_v, max_v = math.floor(min(vs)) - padding, math.ceil(max(vs)) + padding
    width, height = max_u - min_u, max_v - min_v
    registration = parsed.get("painted_registration")
    authored = "albedo_source_kind" in parsed
    if authored and parsed["albedo_source_kind"] != AUTHORED_ALBEDO_KIND:
        raise ExportError("Unsupported authored albedo source kind")
    if authored and registration is None:
        raise ExportError("Authored XYZ palette requires screen registration")
    if registration is not None:
        width, height = registration["source_size"]
        min_u, min_v = (-value for value in registration["pivot_px"])
    if width <= 0 or height <= 0 or width > MAX_DIMENSION or height > MAX_DIMENSION or width * height > MAX_TEXELS:
        raise ExportError(f"snapped output {width}x{height} exceeds {MAX_DIMENSION} dimension/{MAX_TEXELS} texel budget")
    pivot_x, pivot_y = -min_u, -min_v
    if not (0 <= pivot_x <= width and 0 <= pivot_y <= height):
        raise ExportError("snapped pivot is outside source extent")
    depths = [camera_depth(point) for triangle in triangles for point in triangle["vertices"]]
    raw_depth_min, raw_depth_max = min(depths), max(depths)
    if not math.isfinite(raw_depth_min) or not math.isfinite(raw_depth_max) or raw_depth_max - raw_depth_min <= 1e-9:
        raise ExportError("camera depth range is degenerate")
    depth_min, depth_max = float32(raw_depth_min), float32(raw_depth_max)
    if not math.isfinite(depth_min) or not math.isfinite(depth_max):
        raise ExportError("camera depth range is not finite in float32")
    if depth_min == depth_max:
        raise ExportError("camera depth range collapses in float32")
    if depth_min > raw_depth_min:
        depth_min = adjacent_float32(depth_min, False)
    if depth_max < raw_depth_max:
        depth_max = adjacent_float32(depth_max, True)
    if not (depth_min <= raw_depth_min < raw_depth_max <= depth_max):
        raise ExportError("canonical float32 depth range does not contain source geometry")
    if abs(depth_min) > 8192.0 or abs(depth_max) > 8192.0:
        raise ExportError("camera depth range exceeds GameArt [-8192, 8192] contract")
    for source_x in (0.0, float(width)):
        for source_y in (0.0, float(height)):
            projected = ((source_x - pivot_x) / eppp, (source_y - pivot_y) / eppp)
            for depth in (depth_min, depth_max):
                point = reconstruct_xyz(projected, depth)
                if any(not math.isfinite(axis) or abs(axis) > 8192.0 for axis in point):
                    raise ExportError("reconstructed source/depth corner exceeds renderer [-8192, 8192] contract")
    texels = width * height
    albedo = bytearray(texels * 4)
    normal_material = bytearray(texels * 4)
    depth_bytes = bytearray(texels * 2)
    emission = bytearray(texels)
    surface = bytearray(texels * 4)
    caster = bytearray(texels)
    winner_depth = [-math.inf] * texels
    winner_triangle = [-1] * texels
    winner_position: list[tuple[float, float, float] | None] = [None] * texels
    for triangle_index, triangle in enumerate(triangles):
        projected = tuple((point[0] * eppp - min_u, point[1] * eppp - min_v) for point in triangle["projected"])
        x0 = max(0, math.floor(min(point[0] for point in projected)))
        x1 = min(width - 1, math.ceil(max(point[0] for point in projected)) - 1)
        y0 = max(0, math.floor(min(point[1] for point in projected)))
        y1 = min(height - 1, math.ceil(max(point[1] for point in projected)) - 1)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                weights = barycentric((x + 0.5, y + 0.5), projected)
                if weights is None or min(weights) < -1e-10:
                    continue
                position = interpolate(weights, triangle["vertices"])
                depth = camera_depth(position)
                pixel = y * width + x
                if depth <= winner_depth[pixel] + 1e-12:
                    continue
                winner_depth[pixel], winner_triangle[pixel], winner_position[pixel] = depth, triangle_index, position
    for pixel, triangle_index in enumerate(winner_triangle):
        if triangle_index < 0:
            continue
        triangle = triangles[triangle_index]
        a = pixel * 4
        albedo[a:a + 4] = bytes(triangle["albedo"])
        oct_x, oct_y = oct_encode(triangle["normal"])
        normal_material[a:a + 4] = bytes((oct_x, oct_y, round(triangle["roughness"] * 255), round(triangle["ao"] * 255)))
        encoded_depth = round((winner_depth[pixel] - depth_min) / (depth_max - depth_min) * 65535.0)
        depth_bytes[pixel * 2:pixel * 2 + 2] = struct.pack("<H", encoded_depth)
        emission[pixel] = round(triangle["emission_mask"] * 255)
        surface[a:a + 4] = bytes((round(triangle["wet_mask"] * 255), 0, 0, 255))
        caster[pixel] = round(triangle["caster_mask"] * 255)
    if registration is not None and not authored:
        paint = registration["rgba"]
        for pixel in range(texels):
            alpha = paint[pixel * 4 + 3]
            if alpha and winner_triangle[pixel] < 0:
                raise ExportError(f"painted alpha has no authored XYZ geometry at [{pixel % width}, {pixel // width}]")
            if not alpha:
                normal_material[pixel * 4:pixel * 4 + 4] = bytes(4)
                depth_bytes[pixel * 2:pixel * 2 + 2] = bytes(2)
                emission[pixel] = caster[pixel] = 0
                surface[pixel * 4:pixel * 4 + 4] = bytes(4)
            elif registration["caster_alpha_policy"] == "painted-alpha-times-authored-mask":
                caster[pixel] = round(alpha * triangles[winner_triangle[pixel]]["caster_mask"])
        albedo = bytearray(paint)
    return {"width": width, "height": height, "min_u": min_u, "min_v": min_v,
            "depth_range": (depth_min, depth_max), "winner_triangle": winner_triangle,
            "winner_position": winner_position, "albedo": bytes(albedo),
            "normal_material": bytes(normal_material), "camera_depth": bytes(depth_bytes),
            "emission_mask": bytes(emission), "surface_mask": bytes(surface),
            "caster_mask": bytes(caster)}


def build_probes(parsed: dict, raster: dict) -> list[dict]:
    width, height, eppp = raster["width"], raster["height"], parsed["eppp"]
    triangles = parsed["triangles"]
    triangle_lookup = {triangle["id"]: (index, triangle) for index, triangle in enumerate(triangles)}
    output = []
    for request in parsed["probes"]:
        triangle_index, triangle = triangle_lookup[request["triangle_id"]]
        target_world = interpolate(request["barycentric"], triangle["vertices"])
        target_projected = projection(target_world)
        target = (target_projected[0] * eppp - raster["min_u"], target_projected[1] * eppp - raster["min_v"])
        projected = tuple((point[0] * eppp - raster["min_u"], point[1] * eppp - raster["min_v"])
                          for point in triangle["projected"])
        candidates = []
        center_x, center_y = math.floor(target[0]), math.floor(target[1])
        for radius in range(0, 33):
            for y in range(max(0, center_y - radius), min(height, center_y + radius + 1)):
                for x in range(max(0, center_x - radius), min(width, center_x + radius + 1)):
                    pixel = y * width + x
                    if raster["winner_triangle"][pixel] != triangle_index:
                        continue
                    if parsed.get("painted_registration") is not None and raster["albedo"][pixel * 4 + 3] < 64:
                        continue
                    point = (x + 0.5, y + 0.5)
                    clearance = min(edge_distance(point, projected[i], projected[(i + 1) % 3]) for i in range(3))
                    if clearance >= 8.0:
                        candidates.append(((point[0] - target[0]) ** 2 + (point[1] - target[1]) ** 2, y, x, clearance))
            if candidates:
                break
        if not candidates:
            raise ExportError(f"probe {request['id']} has no visible texel at least 8 px from triangle boundaries")
        _, y, x, clearance = min(candidates)
        pixel = y * width + x
        position = raster["winner_position"][pixel]
        assert position is not None
        a = pixel * 4
        output.append({"id": request["id"], "source_pixel": [x + 0.5, y + 0.5],
                       "position": list(position), "normal": list(triangle["normal"]),
                       "albedo": list(raster["albedo"][a:a + 4]),
                       "roughness": triangle["roughness"], "ao": triangle["ao"],
                       "emission_mask": triangle["emission_mask"], "wet_mask": triangle["wet_mask"],
                       "triangle_id": triangle["id"],
                       "triangle_vertices": [list(vertex) for vertex in triangle["vertices"]],
                       "boundary_clearance_px": clearance})
    return output


def manifest_text(rows: list[dict]) -> bytes:
    canonical = ""
    for row in sorted(rows, key=lambda item: item["id"]):
        for field in ("id", "path", "kind", "sha256", "license", "source", "author", "notice"):
            value = row[field]
            canonical += f"{len(value.encode('utf-8'))}:{value}"
        canonical += "\n"
    return (json.dumps({"schema": 1, "content_sha256": sha256(canonical.encode("utf-8")), "assets": rows},
                       indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def export(master_path: Path, output_root: Path) -> None:
    if output_root.exists():
        raise ExportError("output root already exists; exporter refuses to overwrite")
    master_raw = master_path.read_bytes()
    master, parsed = parse_master(master_raw)
    raster = rasterize(parsed)
    probes = build_probes(parsed, raster)
    exporter_hash = sha256(Path(__file__).read_bytes())
    master_hash = sha256(master_raw)
    camera_record = {"id": "ribterra-affine-v1", "u": "29*(X-Y)",
                     "v": "14.5*(X+Y)-29*Z",
                     "camera_depth": "dot(XYZ,(1,1,1)/sqrt(3))",
                     "frontmost": "maximum-camera-depth"}
    camera_hash = sha256(canonical_json(camera_record))
    settings_record = {"camera": camera_record,
                       "export_pixels_per_reference_pixel": parsed["eppp"],
                       "padding_export_pixels": master["export"]["padding_export_pixels"],
                       "raster_sampling": "one-sample-at-texel-center",
                       "interpolation": "affine-barycentric-XYZ",
                       "normal_basis": "flat-face-from-vertex-winding",
                       "depth_storage": "R16_UNORM-tight-little-endian"}
    settings_hash = sha256(canonical_json(settings_record))
    width, height = raster["width"], raster["height"]
    texture_rows = []
    textures = []
    channel_bindings = {}
    files: dict[str, bytes] = {}
    for channel, filename, texture_format, _ in CHANNELS:
        relative = f"art/material/{filename}"
        data = raster[channel]
        texture_id = f"calibration.texture.{channel}"
        files[relative] = data
        texture_rows.append({"id": texture_id, "path": relative, "kind": "data", "sha256": sha256(data),
                             "license": "LicenseRef-Ribterra-Project", "source": master_path.name,
                             "author": "Ribterra project", "notice": "NOTICE.txt", "bytes": len(data)})
        textures.append({"id": texture_id, "path": relative, "format": texture_format, "width": width,
                         "height": height, "layout": "tight_le", "mip_levels": 1,
                         "bytes": len(data), "sha256": sha256(data)})
        channel_bindings[channel] = {"texture": texture_id, "rect": [0, 0, width, height]}
    ids = parsed["ids"]
    material_common = {"family": "DynamicSprite25D", "alpha": "straight", "camera": "ribterra-affine-v1",
                       "revision": 1, "source_hash": master_hash, "channels": channel_bindings}
    materials = [dict(material_common, id=ids["material_off"], emission=[0.0, 0.0, 0.0, 0.0]),
                 dict(material_common, id=ids["material_on"], emission=list(parsed["emission_radiance"]))]
    sprite_common = {"source_size": [float(width), float(height)],
                     "pivot_px": [float(-raster["min_u"]), float(-raster["min_v"])],
                     "depth_range": list(raster["depth_range"]),
                     "export_pixels_per_reference_pixel": parsed["eppp"]}
    sprite_common["local_occluders"] = parsed["local_occluders"]
    sprite_common["sky_shadow_triangles"] = parsed["sky_shadow_triangles"]
    sprites = [dict(sprite_common, id=ids["sprite_off"], material=ids["material_off"]),
               dict(sprite_common, id=ids["sprite_on"], material=ids["material_on"])]
    world = {"schema": 2, "pages": [], "textures": textures, "materials": materials, "lit_sprites": sprites}
    world_bytes = (json.dumps(world, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    files["art/world.json"] = world_bytes
    sky_shadow_hash = sha256(canonical_json(parsed["sky_shadow_triangles"]))
    calibration = {"schema": 1, "master_sha256": master_hash, "camera_sha256": camera_hash,
                   "settings_sha256": settings_hash,
                   "exporter_sha256": exporter_hash, "source_size": [width, height],
                   "pivot_px": sprite_common["pivot_px"], "export_pixels_per_reference_pixel": parsed["eppp"],
                   "depth_range": list(raster["depth_range"]), "sprite_id_off": ids["sprite_off"],
                   "sprite_id_on": ids["sprite_on"], "material_id_off": ids["material_off"],
                   "material_id_on": ids["material_on"], "emission_radiance": list(parsed["emission_radiance"]),
                   "sky_shadow_triangles_sha256": sky_shadow_hash,
                   "sky_shadow_triangle_count": len(parsed["sky_shadow_triangles"]),
                   "export_settings": settings_record, "master_export_inputs": master["export"], "probes": probes}
    calibration_bytes = (json.dumps(calibration, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    files["art/material/calibration.json"] = calibration_bytes
    notice = ("Ribterra deterministic geometry calibration fixture.\n"
              f"Editable master: {master_path.name}\nMaster SHA-256: {master_hash}\n"
              "Purpose: diagnostic material-pipeline calibration; not concept-ready art.\n")
    files["NOTICE.txt"] = notice.encode("utf-8")
    rows = texture_rows + [
        {"id": "art.world", "path": "art/world.json", "kind": "data", "sha256": sha256(world_bytes),
         "license": "LicenseRef-Ribterra-Project", "source": master_path.name, "author": "Ribterra project",
         "notice": "NOTICE.txt", "bytes": len(world_bytes)},
        {"id": "calibration.metadata", "path": "art/material/calibration.json", "kind": "data",
         "sha256": sha256(calibration_bytes), "license": "LicenseRef-Ribterra-Project", "source": master_path.name,
         "author": "Ribterra project", "notice": "NOTICE.txt", "bytes": len(calibration_bytes)}]
    files["manifest.json"] = manifest_text(rows)
    os.mkdir(output_root)
    os.mkdir(output_root / "art")
    os.mkdir(output_root / "art" / "material")
    for relative in sorted(files):
        destination = output_root / relative
        with destination.open("xb") as stream:
            stream.write(files[relative])


def exact_fields(value: object, fields: set[str], label: str) -> dict:
    if not isinstance(value, dict) or set(value) != fields:
        raise ExportError(f"{label} fields must be exactly {', '.join(sorted(fields))}")
    return value


def portable_path(value: object, label: str) -> str:
    if not isinstance(value, str) or not value or any(char in value for char in '\\:<>"|?*\x00'):
        raise ExportError(f"{label} must be a confined repository-relative portable path")
    path = PurePosixPath(value)
    if path.is_absolute() or path.as_posix() != value or any(part in (".", "..") for part in path.parts):
        raise ExportError(f"{label} must be a confined repository-relative portable path")
    for part in path.parts:
        if part.endswith((".", " ")) or re.fullmatch(r"(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?", part):
            raise ExportError(f"{label} contains a reserved path component")
    return value


def no_reparse(path: Path) -> Path:
    path = Path(os.path.abspath(path))
    for component in reversed((path, *path.parents)):
        if os.path.lexists(component):
            info = component.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ExportError(f"Reparse/symlink path is forbidden: {component}")
    return path


class CollectionInputs:
    def __init__(self, repo: Path) -> None:
        self.repo = no_reparse(repo)
        self.before: dict[str, dict] = {}
        self.read_limits: dict[str, int] = {}

    def read(self, relative: str, expected: str | None = None, *, maximum_bytes: int = MAX_COLLECTION_BYTES) -> bytes:
        portable_path(relative, "input path")
        if type(maximum_bytes) is not int or not 0 < maximum_bytes <= MAX_COLLECTION_BYTES:
            raise ExportError("Invalid bounded input read limit")
        maximum_bytes = min(maximum_bytes, self.read_limits.get(relative, MAX_COLLECTION_BYTES))
        path = no_reparse(self.repo / relative)
        if not path.is_relative_to(self.repo) or not path.is_file():
            raise ExportError(f"Input is not a confined regular file: {relative}")
        size = path.stat().st_size
        if size > maximum_bytes:
            label = "128 MiB bound" if maximum_bytes == MAX_COLLECTION_BYTES else f"bounded {maximum_bytes} byte read limit"
            raise ExportError(f"Input exceeds the {label}: {relative}")
        with path.open("rb") as stream:
            data = stream.read(size + 1)
        if len(data) != size or path.stat().st_size != size:
            raise ExportError(f"Input changed while reading: {relative}")
        row = {"relative_path": relative, "bytes": size, "sha256": sha256(data)}
        if expected is not None and (not isinstance(expected, str) or row["sha256"] != expected):
            raise ExportError(f"Declared input hash mismatch: {relative}")
        if relative in self.before and self.before[relative] != row:
            raise ExportError(f"Input changed during export: {relative}")
        self.before[relative] = row
        self.read_limits[relative] = maximum_bytes
        return data

    def recheck(self) -> list[dict]:
        for relative in sorted(self.before):
            self.read(relative, self.before[relative]["sha256"])
        return [self.before[path] for path in sorted(self.before)]


def json_document(data: bytes, label: str) -> dict:
    def nonfinite_constant(value: str) -> None:
        raise ExportError(f"Nonfinite JSON constant in {label}: {value}")

    def unique_fields(pairs: list[tuple[str, object]]) -> dict:
        result = {}
        for name, value in pairs:
            if name in result:
                raise ExportError(f"{label} contains duplicate JSON field: {name}")
            result[name] = value
        return result
    try:
        value = json.loads(data, object_pairs_hook=unique_fields, parse_constant=nonfinite_constant)
    except (ValueError, UnicodeDecodeError) as error:
        raise ExportError(f"{label} is not valid UTF-8 JSON: {error}") from error
    if not isinstance(value, dict):
        raise ExportError(f"{label} must be an object")
    return value


def collection_master(raw: bytes) -> tuple[dict, dict]:
    master = json_document(raw, "collection master")
    allowed = {"schema", "coordinate_system", "purpose", "ids", "emission_radiance", "export",
               "local_occluders", "sky_shadow_triangles", "triangles", "probes"}
    if set(master) - allowed:
        raise ExportError("Unsupported collection master metadata: " + ", ".join(sorted(set(master) - allowed)))
    if type(master.get("schema")) is not int or not isinstance(master.get("purpose", ""), str):
        raise ExportError("Collection master schema/purpose types are invalid")
    exact_fields(master.get("export"), {"export_pixels_per_reference_pixel", "padding_export_pixels"}, "master.export")
    exact_fields(master.get("ids"), {"material_off", "material_on", "sprite_off", "sprite_on"}, "master.ids")
    if not isinstance(master.get("triangles"), list) or not isinstance(master.get("probes"), list):
        raise ExportError("Collection master triangles/probes must be arrays")
    triangle_fields = {"id", "vertices", "albedo", "roughness", "ao", "emission_mask", "wet_mask", "caster_mask"}
    for index, triangle in enumerate(master.get("triangles", [])):
        exact_fields(triangle, triangle_fields, f"master.triangles[{index}]")
    for index, probe in enumerate(master.get("probes", [])):
        exact_fields(probe, {"id", "triangle_id", "barycentric"}, f"master.probes[{index}]")
    return parse_master(raw)


def painted_registration(item: dict, catalog: dict, sources: dict, inputs: CollectionInputs) -> tuple[dict, dict, dict]:
    registration = exact_fields(item["registration"], {
        "kind", "png_sha256", "catalog_entry_sha256", "source_size", "pivot_px",
        "reference_width", "caster_alpha_policy"}, "registration")
    if registration["kind"] != "original-catalog-screen-v1":
        raise ExportError("Unsupported painted registration kind")
    if registration["caster_alpha_policy"] not in ("authored-mask", "painted-alpha-times-authored-mask"):
        raise ExportError("Unsupported caster alpha policy")
    matches = [(page, row) for page in catalog.get("pages", []) for row in page.get("sprites", [])
               if row.get("id") == item["legacy_key"]]
    if len(matches) != 1:
        raise ExportError("Legacy key must resolve to exactly one original catalog entry")
    page, entry = matches[0]
    if set(page) != {"path", "sprites"} or set(entry) - {"id", "rect", "pivot", "reference_width"}:
        raise ExportError("Unsupported original page/sprite metadata or alpha policy")
    entry_record = {"page": {"path": page["path"]}, "sprite": entry}
    if sha256(canonical_json(entry_record)) != registration["catalog_entry_sha256"]:
        raise ExportError("Original catalog entry hash mismatch")
    rect = entry["rect"]
    if not isinstance(rect, list) or len(rect) != 4 or any(type(v) is not int for v in rect) or min(rect[:2]) < 0 or min(rect[2:]) <= 0:
        raise ExportError("Original crop rectangle is invalid")
    width, height = rect[2:]
    if width > MAX_DIMENSION or height > MAX_DIMENSION or width * height > MAX_TEXELS:
        raise ExportError("Original crop exceeds the material raster budget")
    pivot = vector(entry["pivot"], 2, "original pivot")
    if any(v < 0 or v > 1 for v in pivot):
        raise ExportError("Original pivot is outside [0,1]")
    pivot_px = [float32(float32(pivot[0]) * width), float32(float32(pivot[1]) * height)]
    reference_width = float32(finite_number(entry.get("reference_width", width), "original reference_width"))
    if reference_width <= 0 or reference_width > 4096:
        raise ExportError("Original reference width is invalid")
    declared_size = registration["source_size"]
    declared_pivot = vector(registration["pivot_px"], 2, "declared pivot_px")
    declared_reference = finite_number(registration["reference_width"], "declared reference_width")
    if (not isinstance(declared_size, list) or any(type(v) is not int for v in declared_size) or
            declared_size != [width, height] or list(declared_pivot) != pivot_px or declared_reference != reference_width):
        raise ExportError("Declared source extent/pivot/reference width differs from original catalog")
    relative = "assets/" + portable_path(page["path"], "original page path")
    raw = inputs.read(relative, registration["png_sha256"])
    # Simplified libpng and Pillow agree for these untransformed RGBA8 pages.
    # Reject colour profiles/transfer declarations instead of silently converting.
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ExportError("Paint source must be a PNG")
    offset, ihdr, ended = 8, None, False
    while offset + 12 <= len(raw):
        count = struct.unpack_from(">I", raw, offset)[0]
        if offset + count + 12 > len(raw):
            raise ExportError("Truncated painted PNG chunk")
        tag = raw[offset + 4:offset + 8]
        if tag in (b"gAMA", b"sRGB", b"iCCP", b"cHRM", b"acTL"):
            raise ExportError("Unsupported painted PNG colour transform or animation")
        if tag == b"IHDR":
            if ihdr is not None or count != 13:
                raise ExportError("Invalid painted PNG IHDR")
            ihdr = struct.unpack(">IIBBBBB", raw[offset + 8:offset + 21])
        offset += count + 12
        if tag == b"IEND":
            ended = True
            break
    if not ended or offset != len(raw) or ihdr is None or ihdr[2:] != (8, 6, 0, 0, 0) or max(ihdr[:2]) > 4096 or min(ihdr[:2]) < 1:
        raise ExportError("Paint source must be bounded noninterlaced RGBA8 PNG")
    try:
        from PIL import Image, __version__ as pillow_version
        with Image.open(io.BytesIO(raw)) as image:
            if image.mode != "RGBA" or image.size != ihdr[:2] or rect[0] + width > image.width or rect[1] + height > image.height:
                raise ExportError("Paint source/crop dimensions or RGBA mode differ")
            rgba = image.crop((rect[0], rect[1], rect[0] + width, rect[1] + height)).tobytes()
    except ImportError as error:
        raise ExportError("Painted collection requires already-installed Pillow; calibration does not") from error
    except (OSError, ValueError) as error:
        raise ExportError(f"Cannot decode painted PNG: {error}") from error
    if not any(rgba[3::4]):
        raise ExportError("Painted registration must contain nonzero alpha")
    metadata = sources.get("overrides", {}).get(page["path"])
    if not isinstance(metadata, dict):
        raise ExportError("Original painted page needs an explicit asset_sources override")
    provenance = exact_fields(item["provenance"], {"license", "author", "source", "notice_path", "notice_sha256", "asset_source_sha256"}, "provenance")
    if any(not isinstance(provenance[field], str) or not provenance[field].strip() for field in ("license", "author", "source")):
        raise ExportError("Paint provenance licence/author/source must be nonempty strings")
    if sha256(canonical_json(metadata)) != provenance["asset_source_sha256"] or any(
        provenance[field] != metadata.get(field) for field in ("license", "author", "source")) or provenance["notice_path"] != metadata.get("notice"):
        raise ExportError("Declared provenance differs from original asset_sources metadata")
    notice_path = portable_path(provenance["notice_path"], "notice_path")
    if not notice_path.startswith("licenses/"):
        raise ExportError("Original notice must be a repository licenses/ file")
    binding_paths = {relative, notice_path}
    notice = inputs.read(notice_path, provenance["notice_sha256"])
    try:
        notice.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ExportError("Original notice must be UTF-8") from error
    if "prompt_file" in metadata:
        path = portable_path(metadata["prompt_file"], "prompt_file")
        inputs.read(path)
        binding_paths.add(path)
    if not isinstance(metadata.get("source_images", []), list):
        raise ExportError("Original source_images must be an array")
    for source in metadata.get("source_images", []):
        exact_fields(source, {"path", "sha256"}, "asset source image")
        path = portable_path(source["path"], "source image")
        inputs.read(path, source["sha256"])
        binding_paths.add(path)
    for reference in item["references"]:
        exact_fields(reference, {"path", "sha256", "purpose"}, "reference")
        if reference["purpose"] != "visual-only":
            raise ExportError("References must explicitly have visual-only scope")
        path = portable_path(reference["path"], "reference path")
        inputs.read(path, reference["sha256"])
        binding_paths.add(path)
    return {**registration, "rgba": rgba}, {
        "page_path": page["path"], "rect": rect, "pivot": list(pivot), "pivot_px": pivot_px,
        "source_size": [width, height], "reference_width": reference_width,
        "png_sha256": sha256(raw), "decoded_crop_sha256": sha256(rgba),
        "catalog_entry_sha256": registration["catalog_entry_sha256"],
        "caster_alpha_policy": registration["caster_alpha_policy"], "asset_source": metadata,
        "provenance": provenance, "references": item["references"],
        "source_bindings": [inputs.before[path] for path in sorted(binding_paths)]}, {"library": "Pillow", "version": pillow_version}


def neutral_palette(item: dict, master: dict, parsed: dict, registration: dict,
                    inputs: CollectionInputs) -> tuple[dict, bytes, bytes]:
    def bounded_path(value: object, label: str) -> str:
        path = portable_path(value, label)
        if len(path.encode("utf-8")) > 1024:
            raise ExportError("Neutral palette portable path exceeds 1024 UTF-8 bytes")
        return path

    def digest(value: object) -> str:
        if type(value) is not str or re.fullmatch(r"[0-9a-f]{64}", value) is None:
            raise ExportError("Neutral palette digest must be a canonical SHA-256")
        return value

    source = exact_fields(item["albedo_source"], {"kind", "palette_path", "palette_sha256", "provenance"}, "albedo_source")
    if source["kind"] != AUTHORED_ALBEDO_KIND:
        raise ExportError("Unsupported albedo_source kind")
    if registration["caster_alpha_policy"] != "authored-mask":
        raise ExportError("Authored albedo requires authored-mask caster policy")
    provenance = exact_fields(source["provenance"], {"license", "author", "source", "notice_path", "notice_sha256"},
                              "albedo_source.provenance")
    for field in ("license", "author", "source"):
        value = provenance[field]
        if type(value) is not str or not value.strip() or len(value.encode("utf-8")) > 512:
            raise ExportError("albedo_source.provenance text exceeds 512 UTF-8 bytes or is empty/invalid")
    for field, value in (("palette_path", source["palette_path"]), ("notice_path", provenance["notice_path"])):
        bounded_path(value, field)
    digest(source["palette_sha256"])
    digest(provenance["notice_sha256"])
    for path, maximum, label in ((source["palette_path"], MAX_PALETTE_BYTES, "Neutral palette exceeds 512 KiB"),
                                 (provenance["notice_path"], MAX_AUTHORED_NOTICE_BYTES, "Authored notice exceeds 128 KiB")):
        confined = no_reparse(inputs.repo / path)
        if confined.is_file() and confined.stat().st_size > maximum:
            raise ExportError(label)
    palette_raw = inputs.read(source["palette_path"], source["palette_sha256"], maximum_bytes=MAX_PALETTE_BYTES)
    notice = inputs.read(provenance["notice_path"], provenance["notice_sha256"], maximum_bytes=MAX_AUTHORED_NOTICE_BYTES)
    try:
        notice.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ExportError("Authored notice must be UTF-8") from error
    # Reject excessive structural nesting before JSON allocation, ignoring quoted braces.
    depth, quoted, escaped = 0, False, False
    for byte in palette_raw:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (91, 123):
            depth += 1
            if depth > 16:
                raise ExportError("Neutral palette nesting exceeds16")
        elif byte in (93, 125):
            depth -= 1
    palette = json_document(palette_raw, "neutral palette")
    fields = {"schema", "kind", "status", "authoring", "geometry_source", "source_registration_target",
              "original_visual_provenance", "source_geometry_provenance", "references", "source_bindings",
              "draft_notice", "legacy_alpha_gap", "open_delivery", "materials", "triangle_materials"}
    exact_fields(palette, fields, "neutral palette")
    if type(palette["schema"]) is not int or palette["schema"] != 1 or palette["kind"] != "ribterra-neutral-palette-v1":
        raise ExportError("Unsupported neutral palette schema/kind")

    def text_limits(value: object) -> None:
        if type(value) is str and len(value.encode("utf-8")) > 4096:
            raise ExportError("Neutral palette string exceeds 4096 UTF-8 bytes")
        if isinstance(value, dict):
            for key, child in value.items():
                text_limits(key)
                text_limits(child)
        elif isinstance(value, list):
            for child in value:
                text_limits(child)
    text_limits(palette)
    if palette["status"] != "DRAFT / SOURCE ONLY / NOT_ACCEPTED":
        raise ExportError("Neutral palette must retain DRAFT status")
    authoring = exact_fields(palette["authoring"], {"author", "colour_encoding", "alpha_policy", "lighting_policy",
                             "glass_policy", "emission_policy", "proposed_export_mode"}, "neutral palette.authoring")
    if authoring["proposed_export_mode"] != AUTHORED_ALBEDO_KIND or authoring["author"] != provenance["author"]:
        raise ExportError("Neutral palette authoring differs from active provenance/mode")
    if any(type(value) is not str or not value.strip() for value in authoring.values()):
        raise ExportError("Neutral palette authoring values must be nonempty text")
    if palette["draft_notice"] != {"path": provenance["notice_path"], "sha256": provenance["notice_sha256"]}:
        raise ExportError("Neutral palette draft notice differs from active provenance")
    if palette["original_visual_provenance"] != item["provenance"] or palette["source_geometry_provenance"] != item["geometry_provenance"]:
        raise ExportError("Neutral palette original/geometry provenance differs")
    geometry = exact_fields(palette["geometry_source"], {"path", "sha256", "triangle_count", "numeric_probe_count",
                            "sky_triangle_count", "unchanged"}, "neutral palette.geometry_source")
    if geometry["unchanged"] != ["XYZ", "triangle IDs/order/winding", "topology", "ao", "sky_shadow_triangles",
                                 "local_occluders", "export", "probes"]:
        raise ExportError("Neutral palette unchanged geometry declaration differs")
    archived, _ = collection_master(inputs.read(bounded_path(geometry["path"], "geometry_source.path"), digest(geometry["sha256"])))
    counts = {"triangle_count": len(archived["triangles"]), "numeric_probe_count": len(archived["probes"]),
              "sky_triangle_count": len(archived["sky_shadow_triangles"])}
    if any(type(geometry[key]) is not int or geometry[key] != value for key, value in counts.items()):
        raise ExportError("Neutral palette archived geometry counts differ")
    for key in ("coordinate_system", "local_occluders", "sky_shadow_triangles", "export", "probes"):
        if master[key] != archived[key]:
            raise ExportError("Neutral master must preserve archived XYZ geometry")
    if len(master["triangles"]) != counts["triangle_count"] or any(
            any(new[key] != old[key] for key in ("id", "vertices", "ao"))
            for new, old in zip(master["triangles"], archived["triangles"], strict=True)):
        raise ExportError("Neutral master must preserve archived XYZ geometry")
    target = exact_fields(palette["source_registration_target"], {"legacy_key", "original_page", "original_rect",
                          "normalized_pivot", "kind", "png_sha256", "catalog_entry_sha256", "source_size", "pivot_px",
                          "reference_width", "caster_alpha_policy", "active_registration", "extent_policy"},
                          "neutral palette.source_registration_target")
    if target["active_registration"] is not False or target["legacy_key"] != item["legacy_key"] or any(
            target[key] != registration[key] for key in ("png_sha256", "catalog_entry_sha256", "source_size", "pivot_px", "reference_width")) or \
            target["original_page"] != "assets/" + registration["page_path"] or target["original_rect"] != registration["rect"] or \
            target["normalized_pivot"] != registration["pivot"] or target["kind"] != "original-catalog-screen-v1" or \
            target["caster_alpha_policy"] not in ("authored-mask", "painted-alpha-times-authored-mask"):
        raise ExportError("Neutral palette inactive source registration differs")
    if type(target["extent_policy"]) is not str or not target["extent_policy"].strip():
        raise ExportError("Neutral palette extent policy must be nonempty text")
    gap = exact_fields(palette["legacy_alpha_gap"], {"source_nonzero_alpha_pixels", "source_pixels_supported_by_old_geometry",
                       "source_pixels_unsupported_by_old_geometry", "source_map", "neutral_output_alpha_measured", "policy"},
                       "neutral palette.legacy_alpha_gap")
    if any(type(gap[key]) is not int or not 0 <= gap[key] <= MAX_TEXELS for key in (
            "source_nonzero_alpha_pixels", "source_pixels_supported_by_old_geometry", "source_pixels_unsupported_by_old_geometry")) or \
            gap["source_nonzero_alpha_pixels"] != gap["source_pixels_supported_by_old_geometry"] + gap["source_pixels_unsupported_by_old_geometry"] or \
            gap["neutral_output_alpha_measured"] is not False or type(gap["policy"]) is not str or not gap["policy"].strip():
        raise ExportError("Neutral palette archived alpha gap is invalid")
    bounded_path(gap["source_map"], "neutral palette source_map")
    if type(palette["open_delivery"]) is not list or not 1 <= len(palette["open_delivery"]) <= 64 or any(
            type(value) is not str or not value.strip() for value in palette["open_delivery"]):
        raise ExportError("Neutral palette open delivery must be bounded nonempty text")
    for key in ("source_bindings", "references"):
        rows = palette[key]
        if type(rows) is not list or len(rows) > 64:
            raise ExportError(f"Neutral palette {key} count exceeds64 or is not an array")
        seen = set()
        for row in rows:
            expected = {"path", "sha256", "bytes"} if key == "source_bindings" else {"path", "sha256", "purpose"}
            label = "palette source binding" if key == "source_bindings" else "palette reference"
            exact_fields(row, expected, label)
            path = bounded_path(row["path"], key + ".path")
            if path.casefold() in seen:
                raise ExportError("Duplicate " + label)
            seen.add(path.casefold())
            raw = inputs.read(path, digest(row["sha256"]))
            if key == "source_bindings" and (type(row["bytes"]) is not int or row["bytes"] != len(raw)):
                raise ExportError("Neutral palette source binding bytes differ")
            if key == "references" and row["purpose"] != "visual-only":
                raise ExportError("Neutral palette references must be visual-only")
    materials, assignments = palette["materials"], palette["triangle_materials"]
    if type(materials) is not list or not 1 <= len(materials) <= 256:
        raise ExportError("Neutral palette materials count must be1..256")
    if type(assignments) is not list or not 1 <= len(assignments) <= 10000:
        raise ExportError("Neutral palette triangle_materials count must be1..10000")
    table = {}
    for material in materials:
        exact_fields(material, {"id", "albedo_srgb", "roughness", "emission_mask", "wet_mask", "caster_mask"}, "palette material")
        identity = material["id"]
        if type(identity) is not str or not re.fullmatch(r"[a-z][a-z0-9_.-]{0,191}", identity):
            raise ExportError("Invalid palette material ID")
        if identity in table:
            raise ExportError("Duplicate palette material")
        if type(material["albedo_srgb"]) is not list or len(material["albedo_srgb"]) != 3:
            raise ExportError("palette albedo_srgb must contain3 bytes")
        for value in material["albedo_srgb"]:
            if type(value) is not int or not 0 <= value <= 255:
                raise ExportError("palette albedo_srgb byte must be an integer in[0,255]")
        for key in ("roughness", "emission_mask", "wet_mask", "caster_mask"):
            if not 0 <= finite_number(material[key], "palette material scalar " + key) <= 1:
                raise ExportError("palette material scalar must be in[0,1]")
        table[identity] = material
    links = {}
    triangle_ids = {entry["id"] for entry in master["triangles"]}
    for row in assignments:
        exact_fields(row, {"triangle_id", "material_id"}, "palette triangle assignment")
        triangle, material = row["triangle_id"], row["material_id"]
        if type(triangle) is not str or triangle not in triangle_ids:
            raise ExportError("Unknown palette triangle")
        if triangle in links:
            raise ExportError("Duplicate palette triangle assignment")
        if type(material) is not str or material not in table:
            raise ExportError("Unknown palette material")
        links[triangle] = material
    if set(links) != triangle_ids:
        raise ExportError("Palette triangle assignments must cover every master triangle")
    if set(links.values()) != set(table):
        raise ExportError("Unused palette material")
    for row in master["triangles"]:
        material = table[links[row["id"]]]
        if row["albedo"][3] != 255:
            raise ExportError("Authored albedo requires opaque triangle alpha255")
        if row["albedo"][:3] != material["albedo_srgb"]:
            raise ExportError("Master/palette albedo mismatch")
        if any(row[key] != material[key] for key in ("roughness", "emission_mask", "wet_mask", "caster_mask")):
            raise ExportError("Master/palette scalar mismatch")
    parsed["albedo_source_kind"] = AUTHORED_ALBEDO_KIND
    return source, palette_raw, notice


def merge_authored_payload(files: dict[str, bytes], authored_files: dict[str, bytes]) -> None:
    occupied = {path.casefold() for path in files} | {"material-collection-report.json"}
    for path in authored_files:
        key = path.casefold()
        if any(key == prior or key.startswith(prior + "/") or prior.startswith(key + "/") for prior in occupied):
            raise ExportError("Authored collection path collides with another output")
        occupied.add(key)
    files.update(authored_files)


def export_collection(collection_path: Path, output_root: Path) -> None:
    repo = no_reparse(Path(__file__).parent.parent)
    build = no_reparse(repo / "build")
    output_root = no_reparse(output_root)
    if not build.is_dir() or output_root.parent != build or os.path.lexists(output_root):
        raise ExportError("Collection output must be a fresh immediate repository build child")
    portable_path(output_root.name, "output name")
    collection_path = no_reparse(collection_path if collection_path.is_absolute() else repo / collection_path)
    if not collection_path.is_relative_to(repo):
        raise ExportError("Collection input must be confined to the authoritative repository")
    inputs = CollectionInputs(repo)
    collection_relative = collection_path.relative_to(repo).as_posix()
    collection = json_document(inputs.read(collection_relative), "collection")
    exact_fields(collection, {"schema", "kind", "items"}, "collection")
    if type(collection["schema"]) is not int or collection["schema"] != 1 or collection["kind"] != "ribterra-material-collection-v1":
        raise ExportError("Unsupported material collection schema/kind")
    items = collection["items"]
    if not isinstance(items, list) or not 1 <= len(items) <= 32:
        raise ExportError("Collection must contain 1..32 items")
    catalog = json_document(inputs.read("assets/art/world.json"), "original catalog")
    if catalog.get("schema") != 1:
        raise ExportError("Painted registration requires the original schema-1 catalog")
    sources = json_document(inputs.read("content/asset_sources.json"), "asset_sources")
    exporter_hash = sha256(inputs.read("tools/export_material_master.py"))
    files, textures, materials, sprites, metadata_rows = {}, [], [], [], []
    manifest_rows = []
    authored_files, authored_provenances = {}, []
    namespaces, legacy_keys = set(), set()
    decoder = None
    item_fields = {"namespace", "legacy_key", "master_path", "master_sha256", "registration", "provenance", "geometry_provenance", "references"}
    for item in items:
        exact_fields(item, item_fields | ({"albedo_source"} if isinstance(item, dict) and "albedo_source" in item else set()), "collection item")
        for field, used in (("namespace", namespaces), ("legacy_key", legacy_keys)):
            value = item[field]
            if not isinstance(value, str) or not re.fullmatch(r"[a-z][a-z0-9_.-]{0,191}", value) or value in used:
                raise ExportError(f"Duplicate or invalid {field}")
            portable_path(value, field)
            used.add(value)
        geometry = exact_fields(item["geometry_provenance"], {"kind", "author"}, "geometry_provenance")
        if geometry["kind"] != "authored-proxy-xyz-v1" or not isinstance(geometry["author"], str) or not geometry["author"].strip() or len(geometry["author"]) > 256:
            raise ExportError("Geometry provenance must identify authored proxy XYZ and its declared author")
        if not isinstance(item["references"], list) or len(item["references"]) > 64:
            raise ExportError("references must be an array of at most 64 entries")
    if items != sorted(items, key=lambda row: row["namespace"]):
        raise ExportError("Collection items must be sorted by namespace")
    for item in items:
        raw = inputs.read(portable_path(item["master_path"], "master_path"), item["master_sha256"])
        master, parsed = collection_master(raw)
        if parsed["padding"] != 0:
            raise ExportError("Screen-registered masters must declare zero export padding")
        paint, registration, decoder = painted_registration(item, catalog, sources, inputs)
        parsed["painted_registration"] = paint
        authored_source = None
        if "albedo_source" in item:
            authored_source, palette_raw, authored_notice = neutral_palette(item, master, parsed, registration, inputs)
            authored_provenance = authored_source["provenance"]
            authored_provenances.append(authored_provenance)
            for path, data in ((authored_source["palette_path"], palette_raw),
                               (authored_provenance["notice_path"], authored_notice)):
                if path in authored_files and authored_files[path] != data:
                    raise ExportError("Authored collection file identities collide")
                authored_files[path] = data
        raster = rasterize(parsed)
        probes = build_probes(parsed, raster)
        namespace, legacy_key = item["namespace"], item["legacy_key"]
        width, height = raster["width"], raster["height"]
        source_record = {"master_sha256": sha256(raw), "registration": registration,
                         "geometry_provenance": item["geometry_provenance"]}
        if authored_source is not None:
            source_record.update(albedo_source=authored_source, generated_albedo_sha256=sha256(raster["albedo"]))
        source_hash = sha256(canonical_json(source_record))
        provenance = authored_source["provenance"] if authored_source is not None else item["provenance"]
        bindings = {}
        for channel, filename, texture_format, _ in CHANNELS:
            relative, texture_id = f"art/materials/{namespace}/{filename}", f"{namespace}.texture.{channel}"
            data = raster[channel]
            files[relative] = data
            textures.append({"id": texture_id, "path": relative, "format": texture_format,
                             "width": width, "height": height, "layout": "tight_le", "mip_levels": 1,
                             "bytes": len(data), "sha256": sha256(data)})
            bindings[channel] = {"texture": texture_id, "rect": [0, 0, width, height]}
            manifest_rows.append({"id": texture_id, "path": relative, "kind": "data", "sha256": sha256(data),
                                  "bytes": len(data), "license": provenance["license"], "source": collection_relative,
                                  "author": provenance["author"] + "; geometry: " + item["geometry_provenance"]["author"],
                                  "notice": provenance["notice_path"]})
        if authored_source is not None:
            manifest_rows.append({"id": namespace + ".palette", "path": authored_source["palette_path"], "kind": "data",
                                  "sha256": sha256(palette_raw), "bytes": len(palette_raw), "license": provenance["license"],
                                  "source": collection_relative, "author": provenance["author"], "notice": provenance["notice_path"]})
        material_id, sprite_id = f"{namespace}.material", f"dynamic.{legacy_key}"
        materials.append({"id": material_id, "family": "DynamicSprite25D", "alpha": "straight",
                          "camera": "ribterra-affine-v1", "revision": 1, "source_hash": source_hash,
                          "channels": bindings, "emission": list(parsed["emission_radiance"])})
        sprites.append({"id": sprite_id, "material": material_id, "source_size": [float(width), float(height)],
                        "pivot_px": registration["pivot_px"], "depth_range": list(raster["depth_range"]),
                        "export_pixels_per_reference_pixel": parsed["eppp"],
                        "local_occluders": parsed["local_occluders"], "sky_shadow_triangles": parsed["sky_shadow_triangles"]})
        metadata_row = {"namespace": namespace, "legacy_key": legacy_key, "sprite_id": sprite_id,
                              "material_id": material_id, "source_hash": source_hash, **source_record,
                              "export_pixels_per_reference_pixel": parsed["eppp"], "depth_range": list(raster["depth_range"]),
                              "triangle_count": len(parsed["triangles"]), "supported_alpha_samples": sum(a != 0 for a in raster["albedo"][3::4]),
                              "uncovered_alpha_samples": 0, "probes": probes}
        if authored_source is not None:
            original_alpha, generated_alpha = paint["rgba"][3::4], raster["albedo"][3::4]
            original_count = sum(alpha != 0 for alpha in original_alpha)
            generated_count = sum(alpha != 0 for alpha in generated_alpha)
            overlap = sum(a != 0 and b != 0 for a, b in zip(original_alpha, generated_alpha, strict=True))
            metadata_row["coverage"] = {"original_alpha_samples": original_count, "generated_alpha_samples": generated_count,
                                        "overlap_alpha_samples": overlap, "omitted_original_alpha_samples": original_count - overlap,
                                        "generated_only_alpha_samples": generated_count - overlap}
            metadata_row["uncovered_alpha_samples"] = original_count - overlap
        metadata_rows.append(metadata_row)
        original_provenance = item["provenance"]
        notice_path = portable_path(original_provenance["notice_path"], "notice path")
        files[notice_path] = inputs.read(notice_path, original_provenance["notice_sha256"])
        if sum(map(len, files.values())) > MAX_COLLECTION_BYTES:
            raise ExportError("Collection payload exceeds 128 MiB")
    world = {"schema": 2, "pages": [], "textures": textures, "materials": materials, "lit_sprites": sprites}
    all_ids = [row["id"] for rows in (textures, materials, sprites) for row in rows]
    if len(set(all_ids)) != len(all_ids):
        raise ExportError("Collection runtime IDs collide")
    files["art/world.json"] = canonical_json(world)
    metadata = {"schema": 1, "kind": "ribterra-material-collection-v1", "item_count": len(items), "items": metadata_rows,
                "painted_numeric_probe_min_alpha": 64,
                "paint_colour": "legacy-painted-neutral-albedo-acceptance-pending", "reference_fidelity": "NOT_ACCEPTED",
                "geometry_scope": "authored-proxy-XYZ-not-reference-reconstruction", "fine_alpha_sky_shadows": "OPEN",
                "production_master_acceptance": "OPEN", "normal_startup": "OPEN", "full_engine": "OPEN"}
    if authored_provenances:
        metadata["paint_colour"] = "authored-XYZ-palette-draft-and-legacy-painted-neutral-acceptance-pending"
    files["art/material-collection.json"] = canonical_json(metadata)
    notice_paths = {item["provenance"]["notice_path"] for item in items}
    notice_paths |= {provenance["notice_path"] for provenance in authored_provenances}
    files["NOTICE.txt"] = ("Generated registered material collection. Original notices are retained at:\n" +
                           "\n".join(sorted(notice_paths)) +
                           "\nNo neutral-albedo or reference-fidelity acceptance is claimed.\n").encode("utf-8")
    licenses = " AND ".join(sorted({item["provenance"]["license"] for item in items} |
                                  {provenance["license"] for provenance in authored_provenances}))
    authors = "; ".join(sorted({item["provenance"]["author"] for item in items} |
                              {item["geometry_provenance"]["author"] for item in items} |
                              {provenance["author"] for provenance in authored_provenances}))
    for identity, relative in (("art.world", "art/world.json"), ("material.collection.metadata", "art/material-collection.json")):
        data = files[relative]
        manifest_rows.append({"id": identity, "path": relative, "kind": "data", "sha256": sha256(data), "bytes": len(data),
                              "license": licenses, "source": collection_relative, "author": authors, "notice": "NOTICE.txt"})
    if len({row["id"] for row in manifest_rows}) != len(manifest_rows):
        raise ExportError("Collection manifest IDs collide")
    files["manifest.json"] = manifest_text(manifest_rows)
    merge_authored_payload(files, authored_files)
    before, after = inputs.recheck(), inputs.recheck()
    report = {"schema": 1, "complete": True, "item_count": len(items), "exporter_sha256": exporter_hash,
              "decoder": decoder, "inputs_before": before, "inputs_after": after,
              "output_inventory_excluding_report": [{"relative_path": path, "bytes": len(data), "sha256": sha256(data)}
                                                     for path, data in sorted(files.items())],
              "report_filename": "material-collection-report.json", "output_file_count": len(files) + 1,
              "output_total_bytes": 0, "max_output_bytes": MAX_COLLECTION_BYTES, "min_free_bytes": MIN_COLLECTION_FREE_BYTES}
    for _ in range(8):
        report_bytes = canonical_json(report)
        total = sum(map(len, files.values())) + len(report_bytes)
        if report["output_total_bytes"] == total:
            break
        report["output_total_bytes"] = total
    else:
        raise ExportError("Collection report byte ledger did not settle")
    files[report["report_filename"]] = report_bytes
    if total > MAX_COLLECTION_BYTES or shutil.disk_usage(build).free < MIN_COLLECTION_FREE_BYTES + total:
        raise ExportError("Collection exceeds 128 MiB or cannot retain 384 MiB free")
    inputs.recheck()
    if os.path.lexists(output_root):
        raise ExportError("Collection output appeared during validation")
    os.mkdir(output_root)
    directories = {PurePosixPath(path).parent for path in files}
    directories |= {parent for directory in tuple(directories) for parent in directory.parents}
    for directory in sorted(directories - {PurePosixPath(".")}, key=lambda path: (len(path.parts), path.as_posix())):
        os.mkdir(no_reparse(output_root / directory.as_posix()))
    for relative, data in sorted(files.items()):
        with no_reparse(output_root / relative).open("xb") as stream:
            stream.write(data)
    if inputs.recheck() != after:
        raise ExportError("Collection inputs changed; failed output is retained")
    actual = []
    for directory, children, names in os.walk(output_root, followlinks=False):
        no_reparse(Path(directory))
        for child in children:
            no_reparse(Path(directory) / child)
        for name in names:
            path = no_reparse(Path(directory) / name)
            relative = path.relative_to(output_root).as_posix()
            if relative not in files or path.read_bytes() != files[relative]:
                raise ExportError("Collection output changed; failed output is retained")
            actual.append(relative)
    if sorted(actual) != sorted(files) or shutil.disk_usage(build).free < MIN_COLLECTION_FREE_BYTES:
        raise ExportError("Collection output inventory/headroom mismatch; output is retained")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("master", type=Path)
    parser.add_argument("output_root", type=Path)
    parser.add_argument("--collection", action="store_true", help="Export a repository-bound painted material collection")
    arguments = parser.parse_args()
    try:
        if arguments.collection:
            export_collection(arguments.master, arguments.output_root)
        else:
            export(arguments.master, arguments.output_root)
    except (ExportError, OSError) as error:
        print(f"material master export failed: {error}", file=sys.stderr)
        return 2
    print(f"exported deterministic material fixture to {arguments.output_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
