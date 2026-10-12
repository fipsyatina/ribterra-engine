#!/usr/bin/env python3
"""Export a small neutral schema-2 GameArt control kit for diagnostics only."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
from pathlib import Path

import export_material_master as material_master


MAX_CATEGORIES = 32
MAX_TRIANGLES_PER_CATEGORY = 256
MAX_LOCAL_EMITTERS_PER_CATEGORY = 4
MAX_CONTACT_FOOTPRINTS_PER_CATEGORY = 4
CONTACT_MASK_DIMENSION = 32
MAX_LOCAL_COORDINATE = 8192.0
LOCAL_EMITTER_CLEARANCE = 0.002
MAX_OUTPUT_BYTES = 128 * 1024 * 1024
SOURCE_NAME = "export_engine_control_kit.py"
NOTICE = (
    "Ribterra neutral engine control kit.\n"
    "Purpose: deterministic GameView and GameArt diagnostics only.\n"
    "These generated controls are not production art or a production asset catalog.\n"
)
CATEGORY_ID_PATTERN = re.compile(r"control\.[a-z0-9]+(?:[.-][a-z0-9]+)*", re.ASCII)


class ControlKitError(ValueError):
    pass


def _triangle(identifier: str, vertices: list[list[float]], color: tuple[int, int, int, int],
              *, emission: float = 0.0, roughness: float = 0.72, wet_mask: float = 0.0) -> dict:
    return {"id": identifier, "vertices": vertices, "albedo": list(color),
            "roughness": roughness, "ao": 0.88, "emission_mask": emission,
            "wet_mask": wet_mask, "caster_mask": 1.0}


def _quad(identifier: str, vertices: list[list[float]], color: tuple[int, int, int, int],
          *, emission: float = 0.0, roughness: float = 0.72, wet_mask: float = 0.0) -> list[dict]:
    return [_triangle(f"{identifier}.a", [vertices[0], vertices[1], vertices[2]], color,
                      emission=emission, roughness=roughness, wet_mask=wet_mask),
            _triangle(f"{identifier}.b", [vertices[0], vertices[2], vertices[3]], color,
                      emission=emission, roughness=roughness, wet_mask=wet_mask)]


def _box(identifier: str, center: tuple[float, float, float], size: tuple[float, float, float],
         color: tuple[int, int, int, int], *, wet_mask: float = 0.0) -> list[dict]:
    cx, cy, cz = center
    hx, hy, hz = (axis / 2.0 for axis in size)
    p = {
        "000": [cx - hx, cy - hy, cz - hz], "100": [cx + hx, cy - hy, cz - hz],
        "110": [cx + hx, cy + hy, cz - hz], "010": [cx - hx, cy + hy, cz - hz],
        "001": [cx - hx, cy - hy, cz + hz], "101": [cx + hx, cy - hy, cz + hz],
        "111": [cx + hx, cy + hy, cz + hz], "011": [cx - hx, cy + hy, cz + hz],
    }
    faces = (("bottom", "000", "010", "110", "100"),
             ("top", "001", "101", "111", "011"),
             ("south", "000", "100", "101", "001"),
             ("east", "100", "110", "111", "101"),
             ("north", "110", "010", "011", "111"),
             ("west", "010", "000", "001", "011"))
    output: list[dict] = []
    for face, a, b, c, d in faces:
        output.extend(_quad(f"{identifier}.{face}", [p[a], p[b], p[c], p[d]], color,
                            wet_mask=0.0 if face == "bottom" else wet_mask))
    return output


def _obb(center: tuple[float, float], half_extents: tuple[float, float], top: float) -> dict:
    return {"shape": "obb", "center_xy": list(center), "axis_x": [1.0, 0.0],
            "axis_y": [0.0, 1.0], "half_extent_x": half_extents[0],
            "half_extent_y": half_extents[1], "base_z": 0.0, "top_z": top}


def _capsule(center: tuple[float, float], half_length: float, radius: float, top: float) -> dict:
    return {"shape": "capsule", "center_xy": list(center), "axis_x": [1.0, 0.0],
            "axis_y": [0.0, 0.0], "half_length": half_length, "radius": radius,
            "base_z": 0.0, "top_z": top}


def _master(category_id: str, triangles: list[dict], probe_triangle: str,
            occluders: list[dict], *, emission_windows: list[str] | None = None,
            local_emitters: list[dict] | None = None) -> dict:
    stem = category_id.removeprefix("control.")
    sky = [{"id": f"{triangle['id']}.sky", "vertices": triangle["vertices"], "source_mask": 3}
           for triangle in triangles]
    contact_prefixes = {"house": "house.body", "stockpile": "stockpile.base",
                        "workshop": "workshop.body", "tree": "tree.trunk", "rock": "rock.base"}
    contact_prefix = contact_prefixes.get(stem, f"{stem}.body")
    contact_footprints = [{"id": "base", "geometry_prefix": contact_prefix,
                           "shape": "box" if stem in {"house", "stockpile", "workshop"} else "ellipse",
                           "margin": 0.12, "opacity": 0.65, "plane_tolerance": 0.03}]
    if stem in {"resident.log", "resident.sack"}:
        contact_footprints.append({"id": "cargo", "geometry_prefix": f"{stem}.cargo",
                                   "shape": "box" if stem.endswith("log") else "ellipse",
                                   "margin": 0.12, "opacity": 0.4, "plane_tolerance": 0.03})
    return {
        "schema": 1, "coordinate_system": "ribterra-world-xyz-v1",
        "diagnostic_category": category_id, "geometry_scale": 1.0,
        "emission_windows": emission_windows or [],
        "export": {"export_pixels_per_reference_pixel": 3.0, "padding_export_pixels": 2},
        "ids": {"material_off": f"{category_id}.material.off",
                "material_on": f"{category_id}.material.on",
                "sprite_off": f"{category_id}.sprite.off",
                "sprite_on": f"{category_id}.sprite.on"},
        "emission_radiance": [2.4, 1.45, 0.55, 1.0] if emission_windows else [0.0, 0.0, 0.0, 0.0],
        "local_occluders": occluders, "local_emitters": local_emitters or [],
        "sky_shadow_triangles": sky,
        "contact_footprints": contact_footprints,
        "triangles": triangles,
        "probes": [{"id": f"{stem}.probe.{index:02d}", "triangle_id": probe_triangle,
                    "barycentric": [0.34, 0.33, 0.33]} for index in range(12)],
    }


def category_masters() -> tuple[dict, ...]:
    # Explicit diagnostic exterior policy V1. These masks test the material
    # path; they are not final production art or accepted visual balancing.
    wall, roof = (132, 126, 112, 255), (91, 82, 75, 255)
    house = _box("house.body", (0.0, 0.0, 0.8), (2.4, 1.8, 1.6), wall, wet_mask=0.8)
    house += _quad("house.roof.east", [[-1.3, -1.0, 1.6], [1.3, -1.0, 1.6],
                   [1.3, 0.0, 2.25], [-1.3, 0.0, 2.25]], roof, wet_mask=1.0)
    house += _quad("house.roof.west", [[-1.3, 0.0, 2.25], [1.3, 0.0, 2.25],
                   [1.3, 1.0, 1.6], [-1.3, 1.0, 1.6]], roof, wet_mask=1.0)
    window_color = (216, 174, 91, 255)
    house += _quad("house.window.left", [[-0.82, 0.91, 0.58], [-0.22, 0.91, 0.58],
                   [-0.22, 0.91, 1.18], [-0.82, 0.91, 1.18]], window_color, emission=1.0)
    house += _quad("house.window.right", [[0.22, 0.91, 0.58], [0.82, 0.91, 0.58],
                   [0.82, 0.91, 1.18], [0.22, 0.91, 1.18]], window_color, emission=1.0)

    stockpile = _box("stockpile.base", (0.0, 0.0, 0.18), (2.6, 1.7, 0.36), (111, 91, 65, 255), wet_mask=0.75)
    stockpile += _box("stockpile.stack.left", (-0.65, 0.15, 0.64), (0.75, 0.75, 0.92), (130, 111, 78, 255), wet_mask=0.8)
    stockpile += _box("stockpile.stack.right", (0.45, -0.15, 0.52), (1.1, 0.7, 0.68), (105, 112, 98, 255), wet_mask=0.8)

    workshop = _box("workshop.body", (0.0, 0.0, 0.75), (2.8, 2.0, 1.5), (116, 121, 117, 255), wet_mask=0.8)
    workshop += _box("workshop.chimney", (0.82, 0.35, 1.9), (0.42, 0.42, 2.3), (75, 76, 73, 255), wet_mask=0.9)
    workshop += _box("workshop.awning", (-0.55, 1.08, 1.0), (1.25, 0.35, 0.22), (88, 83, 75, 255), wet_mask=1.0)

    tree = _box("tree.trunk", (0.0, 0.0, 0.85), (0.42, 0.42, 1.7), (99, 78, 56, 255), wet_mask=0.9)
    tree += _box("tree.crown.low", (0.0, 0.0, 1.75), (1.55, 1.3, 0.9), (76, 109, 73, 255), wet_mask=1.0)
    tree += _box("tree.crown.high", (0.12, -0.08, 2.35), (1.05, 0.95, 0.72), (67, 99, 64, 255), wet_mask=1.0)

    rock = _box("rock.base", (0.0, 0.0, 0.33), (1.65, 1.3, 0.66), (107, 108, 107, 255), wet_mask=0.9)
    rock += _box("rock.crown", (0.18, -0.06, 0.73), (1.0, 0.82, 0.48), (126, 125, 121, 255), wet_mask=0.9)

    def resident(kind: str, held: str | None) -> list[dict]:
        rows = _box(f"resident.{kind}.body", (0.0, 0.0, 0.82), (0.5, 0.38, 1.25), (119, 116, 107, 255), wet_mask=0.65)
        rows += _box(f"resident.{kind}.head", (0.0, 0.0, 1.62), (0.48, 0.46, 0.48), (151, 126, 103, 255))
        if held == "log":
            rows += _box("resident.log.cargo", (0.0, 0.32, 0.86), (1.15, 0.24, 0.24), (105, 76, 49, 255), wet_mask=0.85)
        if held == "sack":
            rows += _box("resident.sack.cargo", (0.38, 0.18, 0.68), (0.52, 0.48, 0.72), (138, 126, 88, 255), wet_mask=0.6)
        return rows

    resident_empty = resident("empty", None)
    resident_log = resident("log", "log")
    resident_sack = resident("sack", "sack")
    house_emitters = [
        {"id": "house.window.left", "anchor_xyz": [-0.52, 0.93, 0.88], "radius": 4.5,
         "direction_cone": [0.0, 0.70710677, -0.70710677, 0.2], "inner_cosine": 0.7,
         "falloff": 2.0, "receiver_mask": 1},
        {"id": "house.window.right", "anchor_xyz": [0.52, 0.93, 0.88], "radius": 4.5,
         "direction_cone": [0.0, 0.70710677, -0.70710677, 0.2], "inner_cosine": 0.7,
         "falloff": 2.0, "receiver_mask": 1},
    ]
    return (
        _master("control.house", house, "house.roof.west.a", [_obb((0.0, 0.0), (1.2, 0.9), 2.25)],
                emission_windows=["house.window.left", "house.window.right"],
                local_emitters=house_emitters),
        _master("control.stockpile", stockpile, "stockpile.stack.left.top.a",
                [_obb((0.0, 0.0), (1.3, 0.85), 1.1)]),
        _master("control.workshop", workshop, "workshop.body.top.a",
                [_obb((0.0, 0.0), (1.4, 1.0), 3.05)]),
        _master("control.tree", tree, "tree.crown.low.top.a", [_capsule((0.0, 0.0), 0.0, 0.72, 2.71)]),
        _master("control.rock", rock, "rock.crown.top.a", [_obb((0.0, 0.0), (0.82, 0.65), 0.97)]),
        _master("control.resident.empty", resident_empty, "resident.empty.body.east.a",
                [_capsule((0.0, 0.0), 0.0, 0.25, 1.86)]),
        _master("control.resident.log", resident_log, "resident.log.body.east.a",
                [_capsule((0.0, 0.0), 0.0, 0.25, 1.86), _obb((0.0, 0.32), (0.575, 0.12), 0.98)]),
        _master("control.resident.sack", resident_sack, "resident.sack.head.east.a",
                [_capsule((0.0, 0.0), 0.0, 0.25, 1.86), _obb((0.38, 0.18), (0.26, 0.24), 1.04)]),
    )


def _finite_number(value: object, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ControlKitError(f"{label} must be a finite number")
    try:
        converted = float(value)
    except (OverflowError, ValueError):
        raise ControlKitError(f"{label} must be a finite number") from None
    if not math.isfinite(converted):
        raise ControlKitError(f"{label} must be a finite number")
    return converted


def _number_vector(value: object, count: int, label: str) -> tuple[float, ...]:
    if not isinstance(value, list) or len(value) != count:
        raise ControlKitError(f"{label} must contain exactly {count} finite numbers")
    return tuple(_finite_number(axis, f"{label}[{index}]") for index, axis in enumerate(value))


def _float32(value: float, label: str = "local emitter number") -> float:
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except (OverflowError, struct.error):
        raise ControlKitError(f"{label} is outside the finite float32 domain") from None


def _canonical_local_emitter(emitter: dict, label: str) -> dict:
    return {
        "id": emitter["id"],
        "anchor_xyz": [_float32(axis) for axis in
                       _number_vector(emitter["anchor_xyz"], 3, f"{label}.anchor_xyz")],
        "radius": _float32(_finite_number(emitter["radius"], f"{label}.radius")),
        "direction_cone": [_float32(axis) for axis in
                           _number_vector(emitter["direction_cone"], 4, f"{label}.direction_cone")],
        "inner_cosine": _float32(_finite_number(emitter["inner_cosine"], f"{label}.inner_cosine")),
        "falloff": _float32(_finite_number(emitter["falloff"], f"{label}.falloff")),
        "receiver_mask": emitter["receiver_mask"],
    }


def _within_occluder_clearance(anchor: tuple[float, ...], occluder: dict) -> bool:
    anchor_x, anchor_y, anchor_z = (_float32(axis) for axis in anchor)
    center = tuple(_float32(axis) for axis in occluder.get("center_xy", [0.0, 0.0]))
    dx, dy = anchor_x - center[0], anchor_y - center[1]
    axis_x = tuple(_float32(axis) for axis in occluder.get("axis_x", [1.0, 0.0]))
    base_z = _float32(occluder["base_z"])
    top_z = _float32(occluder["top_z"])
    within_height = (anchor_z >= base_z - LOCAL_EMITTER_CLEARANCE and
                     anchor_z <= top_z + LOCAL_EMITTER_CLEARANCE)
    if occluder.get("shape") == "obb":
        axis_y = tuple(_float32(axis) for axis in occluder.get("axis_y", [0.0, 1.0]))
        local_x = dx * axis_x[0] + dy * axis_x[1]
        local_y = dx * axis_y[0] + dy * axis_y[1]
        return (within_height and
                abs(local_x) <= _float32(occluder["half_extent_x"]) + LOCAL_EMITTER_CLEARANCE and
                abs(local_y) <= _float32(occluder["half_extent_y"]) + LOCAL_EMITTER_CLEARANCE)
    if occluder.get("shape") == "capsule":
        half_length = _float32(occluder["half_length"])
        along = max(-half_length, min(half_length, dx * axis_x[0] + dy * axis_x[1]))
        nearest_x = center[0] + along * axis_x[0]
        nearest_y = center[1] + along * axis_x[1]
        return (within_height and
                math.hypot(anchor_x - nearest_x, anchor_y - nearest_y) <=
                _float32(occluder["radius"]) + LOCAL_EMITTER_CLEARANCE)
    return False


def _validate_local_emitters(master: dict, category_id: str) -> None:
    emitters = master.get("local_emitters")
    if not isinstance(emitters, list) or len(emitters) > MAX_LOCAL_EMITTERS_PER_CATEGORY:
        raise ControlKitError(
            f"{category_id} local_emitters must be an array with at most {MAX_LOCAL_EMITTERS_PER_CATEGORY} entries")
    emitter_ids: set[str] = set()
    expected_fields = {"id", "anchor_xyz", "radius", "direction_cone", "inner_cosine",
                       "falloff", "receiver_mask"}
    for index, emitter in enumerate(emitters):
        label = f"{category_id} local_emitters[{index}]"
        if not isinstance(emitter, dict) or set(emitter) != expected_fields:
            raise ControlKitError(f"{label} must contain exactly the authored local-emitter fields")
        emitter_id = emitter["id"]
        if (not isinstance(emitter_id, str) or not emitter_id.strip() or
                len(emitter_id.encode("utf-8")) > 256):
            raise ControlKitError(f"{label}.id must be a non-whitespace string of at most 256 UTF-8 bytes")
        if emitter_id in emitter_ids:
            raise ControlKitError(f"{category_id} duplicate local emitter id: {emitter_id}")
        emitter_ids.add(emitter_id)
        canonical = _canonical_local_emitter(emitter, label)
        anchor = tuple(canonical["anchor_xyz"])
        if any(abs(axis) > MAX_LOCAL_COORDINATE for axis in anchor):
            raise ControlKitError(f"{label}.anchor_xyz exceeds the local render bound")
        radius = canonical["radius"]
        if radius <= 0.0 or radius > 32.0:
            raise ControlKitError(f"{label}.radius must be within (0, 32]")
        direction = canonical["direction_cone"]
        direction_length_squared = sum(axis * axis for axis in direction[:3])
        if (any(abs(axis) > 1.0 for axis in direction[:3]) or
                abs(direction_length_squared - 1.0) > 1.0e-4 or
                direction[3] < -1.0 or direction[3] > 1.0):
            raise ControlKitError(f"{label}.direction_cone must have unit XYZ and outer cosine in [-1, 1]")
        inner = canonical["inner_cosine"]
        point_source = direction[3] == -1.0 and inner == -1.0
        if inner < -1.0 or inner > 1.0 or (not point_source and inner <= direction[3]):
            raise ControlKitError(
                f"{label}.inner_cosine must form a point pair at -1 or exceed outer cosine within [-1, 1]")
        falloff = canonical["falloff"]
        if falloff <= 0.0 or falloff > 16.0:
            raise ControlKitError(f"{label}.falloff must be within (0, 16]")
        receiver_mask = emitter["receiver_mask"]
        if (isinstance(receiver_mask, bool) or not isinstance(receiver_mask, int) or
                receiver_mask < 1 or receiver_mask > 255):
            raise ControlKitError(f"{label}.receiver_mask must be an integer within [1, 255]")
        if any(
                _within_occluder_clearance(anchor, occluder)
                for occluder in master.get("local_occluders", [])):
            raise ControlKitError(
                f"{label}.anchor_xyz must clear its owner occluder volume by more than {LOCAL_EMITTER_CLEARANCE}")
    if emitters:
        emission = _number_vector(master.get("emission_radiance"), 4,
                                  f"{category_id} emission_radiance")
        if max(emission[:3]) <= 0.0 or emission[3] <= 0.0:
            raise ControlKitError(
                f"{category_id} local emitters require nonzero effective material emission")
    if category_id == "control.house":
        expected_ids = {"house.window.left", "house.window.right"}
        if emitter_ids != expected_ids:
            raise ControlKitError("control.house must author exactly the two eligible window emitters")
    elif emitters:
        raise ControlKitError(f"{category_id} must not author local emitters")


def _contact_descriptor(master: dict, footprint: dict, label: str) -> dict:
    expected = {"id", "geometry_prefix", "shape", "margin", "opacity", "plane_tolerance"}
    if not isinstance(footprint, dict) or set(footprint) != expected:
        raise ControlKitError(f"{label} must contain exactly the authored contact-footprint fields")
    identifier, prefix = footprint["id"], footprint["geometry_prefix"]
    if (not isinstance(identifier, str) or
            re.fullmatch(r"[a-z][a-z0-9-]{0,63}", identifier, re.ASCII) is None):
        raise ControlKitError(f"{label}.id must be a safe lowercase contact identifier")
    if not isinstance(prefix, str) or not prefix or len(prefix) > 256:
        raise ControlKitError(f"{label}.geometry_prefix must name authored geometry")
    if not isinstance(footprint["shape"], str) or footprint["shape"] not in {"box", "ellipse"}:
        raise ControlKitError(f"{label}.shape must be box or ellipse")
    margin = _float32(_finite_number(footprint["margin"], f"{label}.margin"), f"{label}.margin")
    opacity = _float32(_finite_number(footprint["opacity"], f"{label}.opacity"), f"{label}.opacity")
    tolerance = _float32(_finite_number(footprint["plane_tolerance"], f"{label}.plane_tolerance"),
                         f"{label}.plane_tolerance")
    if not 0.0 <= margin <= 0.5 or not 0.0 <= opacity <= 1.0:
        raise ControlKitError(f"{label} margin/opacity is outside its contact bound")
    if not _float32(0.02) <= tolerance <= _float32(0.04):
        raise ControlKitError(f"{label}.plane_tolerance must be within [0.02, 0.04]")
    vertices = []
    for triangle in master["triangles"]:
        if not isinstance(triangle, dict):
            raise ControlKitError(f"{label}.geometry must use authored triangle records")
        triangle_id = triangle.get("id")
        if not isinstance(triangle_id, str) or not triangle_id.startswith(prefix + "."):
            continue
        positions = triangle.get("vertices")
        if not isinstance(positions, list) or len(positions) != 3:
            raise ControlKitError(f"{label}.geometry triangle must have exactly three vertices")
        vertices.extend(positions)
    if not vertices:
        raise ControlKitError(f"{label}.geometry_prefix has no authored triangles")
    points = [_number_vector(vertex, 3, f"{label}.geometry") for vertex in vertices]
    low = [min(point[axis] for point in points) for axis in range(2)]
    high = [max(point[axis] for point in points) for axis in range(2)]
    anchor = [_float32((low[axis] + high[axis]) * 0.5, f"{label}.geometry") for axis in range(2)] + [0.0]
    extent = [_float32((high[axis] - low[axis]) * 0.5 + margin, f"{label}.geometry") for axis in range(2)]
    if (any(abs(axis) > MAX_LOCAL_COORDINATE for axis in anchor) or
            any(axis < _float32(0.0001) or axis > MAX_LOCAL_COORDINATE for axis in extent)):
        raise ControlKitError(f"{label} geometry-derived footprint exceeds its local bound")
    return {"anchor_local": anchor, "half_extent": extent, "pivot": [0.5, 0.5],
            "opacity": opacity, "plane_tolerance": tolerance}


def _validate_contact_footprints(master: dict, category_id: str) -> None:
    rows = master.get("contact_footprints")
    if not isinstance(rows, list) or not 1 <= len(rows) <= MAX_CONTACT_FOOTPRINTS_PER_CATEGORY:
        raise ControlKitError(f"{category_id} contact_footprints must contain between 1 and 4 entries")
    identifiers: set[str] = set()
    for index, footprint in enumerate(rows):
        label = f"{category_id} contact_footprints[{index}]"
        _contact_descriptor(master, footprint, label)
        if footprint["id"] in identifiers:
            raise ControlKitError(f"{category_id} duplicate contact footprint id: {footprint['id']}")
        identifiers.add(footprint["id"])


def _contact_mask(shape: str) -> bytes:
    # Receiver-plane footprint, not the affine sprite/caster raster. The mask
    # adds only ambient occlusion; lighting and glow remain runtime inputs.
    dimension = CONTACT_MASK_DIMENSION
    values = bytearray(dimension * dimension)
    for y in range(1, dimension - 1):
        for x in range(1, dimension - 1):
            u = 2.0 * (x + 0.5) / dimension - 1.0
            v = 2.0 * (y + 0.5) / dimension - 1.0
            radius = max(abs(u), abs(v)) if shape == "box" else math.hypot(u, v)
            values[y * dimension + x] = round(255.0 * max(0.0, 1.0 - radius) ** 0.5)
    return bytes(values)


def validate_categories(categories: tuple[dict, ...] | list[dict]) -> None:
    if not 1 <= len(categories) <= MAX_CATEGORIES:
        raise ControlKitError(f"category count must be within [1, {MAX_CATEGORIES}]")
    ids: set[str] = set()
    slugs: set[str] = set()
    for index, master in enumerate(categories):
        category_id = master.get("diagnostic_category") if isinstance(master, dict) else None
        if not isinstance(category_id, str) or CATEGORY_ID_PATTERN.fullmatch(category_id) is None:
            raise ControlKitError(
                f"category[{index}] diagnostic_category must use safe lowercase ASCII control.* segments")
        if category_id in ids:
            raise ControlKitError(f"duplicate diagnostic category: {category_id}")
        ids.add(category_id)
        slug = category_id.removeprefix("control.").replace(".", "-")
        if slug in slugs:
            raise ControlKitError(f"diagnostic category path collision: {category_id} -> {slug}")
        slugs.add(slug)
        triangles = master.get("triangles")
        if not isinstance(triangles, list) or not 1 <= len(triangles) <= MAX_TRIANGLES_PER_CATEGORY:
            raise ControlKitError(
                f"{category_id} triangle count must be within [1, {MAX_TRIANGLES_PER_CATEGORY}]")
        if master.get("geometry_scale") != 1.0:
            raise ControlKitError(f"{category_id} geometry_scale must be 1")
        _validate_local_emitters(master, category_id)
        _validate_contact_footprints(master, category_id)


def _row(identifier: str, path: str, data: bytes) -> dict:
    return {"id": identifier, "path": path, "kind": "data", "sha256": material_master.sha256(data),
            "license": "LicenseRef-Ribterra-Project", "source": SOURCE_NAME,
            "author": "Ribterra project", "notice": "NOTICE.txt", "bytes": len(data)}


def export(output_root: Path, categories: tuple[dict, ...] | list[dict] | None = None) -> None:
    if output_root.exists():
        raise ControlKitError("output root already exists; exporter refuses to overwrite")
    category_rows = tuple(categories) if categories is not None else category_masters()
    validate_categories(category_rows)
    files: dict[str, bytes] = {}
    textures: list[dict] = []
    materials: list[dict] = []
    sprites: list[dict] = []
    contact_arts: list[dict] = []
    metadata_categories: list[dict] = []
    manifest_rows: list[dict] = []
    for master in category_rows:
        master_bytes = material_master.canonical_json(master)
        _, parsed = material_master.parse_master(master_bytes)
        raster = material_master.rasterize(parsed)
        probes = material_master.build_probes(parsed, raster)
        category_id = master["diagnostic_category"]
        slug = category_id.removeprefix("control.").replace(".", "-")
        width, height = raster["width"], raster["height"]
        bindings: dict[str, dict] = {}
        for channel, filename, texture_format, _ in material_master.CHANNELS:
            relative = f"art/control/{slug}/{filename}"
            data = raster[channel]
            texture_id = f"{category_id}.texture.{channel}"
            files[relative] = data
            manifest_rows.append(_row(texture_id, relative, data))
            textures.append({"id": texture_id, "path": relative, "format": texture_format,
                             "width": width, "height": height, "layout": "tight_le", "mip_levels": 1,
                             "bytes": len(data), "sha256": material_master.sha256(data)})
            bindings[channel] = {"texture": texture_id, "rect": [0, 0, width, height]}
        master_hash = material_master.sha256(master_bytes)
        material_id = f"{category_id}.material"
        sprite_id = f"{category_id}.sprite"
        local_emitters = [_canonical_local_emitter(
            emitter, f"{category_id} local_emitters[{index}]")
            for index, emitter in enumerate(master["local_emitters"])]
        contacts: list[dict] = []
        category_contact_arts: list[dict] = []
        for index, footprint in enumerate(master["contact_footprints"]):
            art_id = f"{category_id}.contact.{footprint['id']}"
            relative = f"art/control/{slug}/contact-{footprint['id']}.rtex"
            data = _contact_mask(footprint["shape"])
            files[relative] = data
            manifest_rows.append(_row(art_id, relative, data))
            descriptor = {"id": art_id, "path": relative, "format": "r8_unorm", "layout": "tight_le",
                          "width": CONTACT_MASK_DIMENSION, "height": CONTACT_MASK_DIMENSION,
                          "bytes": len(data), "sha256": material_master.sha256(data),
                          "region": [0, 0, CONTACT_MASK_DIMENSION, CONTACT_MASK_DIMENSION],
                          "facing": "omnidirectional"}
            contact_arts.append(descriptor)
            category_contact_arts.append(descriptor)
            contacts.append({"art": art_id, **_contact_descriptor(
                master, footprint, f"{category_id} contact_footprints[{index}]")})
        materials.append({"id": material_id, "family": "DynamicSprite25D", "alpha": "straight",
                          "camera": "ribterra-affine-v1", "revision": 1, "source_hash": master_hash,
                          "channels": bindings, "emission": list(parsed["emission_radiance"])})
        sprite = {"id": sprite_id, "material": material_id,
                  "source_size": [float(width), float(height)],
                  "pivot_px": [float(-raster["min_u"]), float(-raster["min_v"])],
                  "depth_range": list(raster["depth_range"]),
                  "export_pixels_per_reference_pixel": parsed["eppp"],
                  "local_occluders": parsed["local_occluders"],
                  "local_emitters": local_emitters,
                  "contact_shadows": contacts,
                  "sky_shadow_triangles": parsed["sky_shadow_triangles"]}
        sprites.append(sprite)
        metadata_categories.append({"id": category_id, "sprite_id": sprite_id, "material_id": material_id,
                                    "geometry_scale": 1.0, "master_sha256": master_hash,
                                    "source_size": [width, height], "pivot_px": sprite["pivot_px"],
                                    "depth_range": sprite["depth_range"],
                                    "triangle_count": len(parsed["triangles"]),
                                    "sky_shadow_triangle_count": len(parsed["sky_shadow_triangles"]),
                                    "sky_source_mask": 3,
                                    "silhouette_sha256": material_master.sha256(raster["caster_mask"]),
                                    "emission_windows": list(master["emission_windows"]),
                                    "local_emitters": local_emitters,
                                    "contact_shadows": contacts, "contact_arts": category_contact_arts,
                                    "contact_source_geometry": master["contact_footprints"],
                                    "probe_scope": "one-representative-visible-face", "probes": probes})
    world = {"schema": 2, "pages": [], "textures": textures,
             "materials": materials, "lit_sprites": sprites, "contact_arts": contact_arts}
    world_bytes = (json.dumps(world, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    metadata = {"schema": 1, "purpose": "diagnostic-neutral-controls-only",
                "wet_mask_policy": "diagnostic-exterior-v1",
                "production_catalog": False, "geometry_scale": 1.0,
                "category_count": len(metadata_categories), "contact_art_count": len(contact_arts),
                "categories": metadata_categories}
    metadata_bytes = (json.dumps(metadata, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    files["art/world.json"] = world_bytes
    files["art/control-kit.json"] = metadata_bytes
    files["NOTICE.txt"] = NOTICE.encode("utf-8")
    manifest_rows.extend((_row("art.world", "art/world.json", world_bytes),
                          _row("control.metadata", "art/control-kit.json", metadata_bytes)))
    files["manifest.json"] = material_master.manifest_text(manifest_rows)
    total_bytes = sum(len(data) for data in files.values())
    if total_bytes > MAX_OUTPUT_BYTES:
        raise ControlKitError(f"generated output {total_bytes} bytes exceeds {MAX_OUTPUT_BYTES} byte budget")
    os.mkdir(output_root)
    os.mkdir(output_root / "art")
    os.mkdir(output_root / "art" / "control")
    for master in category_rows:
        slug = master["diagnostic_category"].removeprefix("control.").replace(".", "-")
        os.mkdir(output_root / "art" / "control" / slug)
    for relative in sorted(files):
        with (output_root / relative).open("xb") as stream:
            stream.write(files[relative])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_root", type=Path)
    arguments = parser.parse_args()
    try:
        export(arguments.output_root)
    except (ControlKitError, material_master.ExportError, OSError) as error:
        print(f"engine control kit export failed: {error}", file=sys.stderr)
        return 2
    print(f"exported diagnostic engine control kit to {arguments.output_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
