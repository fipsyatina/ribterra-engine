#!/usr/bin/env python3
"""Retained CPU checks for explicit XYZ/palette export; SOURCE pending execution.

This checks a provisional opaque house and the unchanged painted resident proxy.
It does not accept physical materials, reference fidelity, GPU rendering or main.
Every generated file, failed output and diagnostic input is deliberately retained.
"""
from __future__ import annotations

import argparse
from array import array
import copy
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
MODE = "authored-xyz-triangle-materials-v1"
MAX_BYTES = 128 * 1024 * 1024
MIN_FREE = 384 * 1024 * 1024
CHANNELS = {
    "albedo": ("albedo.rtex", "rgba8_unorm_srgb", 4),
    "normal_material": ("normal-material.rtex", "rgba8_unorm", 4),
    "camera_depth": ("camera-depth.rtex", "r16_unorm", 2),
    "emission_mask": ("emission-mask.rtex", "r8_unorm", 1),
    "surface_mask": ("surface-mask.rtex", "rgba8_unorm", 4),
    "caster_mask": ("caster-mask.rtex", "r8_unorm", 1),
}
PALETTE_FIELDS = {
    "schema", "kind", "status", "materials", "triangle_materials", "authoring",
    "geometry_source", "source_registration_target", "original_visual_provenance",
    "source_geometry_provenance", "references", "source_bindings", "draft_notice",
    "legacy_alpha_gap", "open_delivery",
}
OLD_DESCRIPTOR = "build/material-collection-tests-2026-10-08-attempt02/fixtures/collection.json"
DEFAULT_DESCRIPTOR = "art-source/production-materials-v1/house-neutral-collection.json"
CALIBRATION_MASTER_SHA = "6b362340248c1b8eef3a6ebe850fb01d1962485d910d9605a6d25b087b923359"
CALIBRATION_EXPORTER_SHA = "0616a51fc385ee0ed3d9aa7028f7ae6d6c23b03438c8786d91fc55c9a8cac8f4"
RETAINED_ALIASES: dict[Path, Path] = {}
CALIBRATION_METADATA_PATH = "art/material/calibration.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def json_read(path: Path) -> dict:
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    return json.loads(path.read_bytes(), object_pairs_hook=pairs)


def safe_path(path: Path) -> Path:
    path = Path(os.path.abspath(path))
    for parent in reversed((path, *path.parents)):
        if os.path.lexists(parent):
            info = parent.lstat()
            require(not parent.is_symlink() and not
                    (getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT),
                    f"Reparse path is forbidden: {parent}")
    return path


def write(path: Path, data: bytes) -> None:
    safe_path(path)
    require(len(data) <= MAX_BYTES, "One retained file exceeds 128 MiB")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)


def tree(root: Path) -> dict[str, bytes]:
    result = {}
    for directory, children, names in os.walk(safe_path(root), followlinks=False):
        for name in list(children):
            child = Path(directory) / name
            if child in RETAINED_ALIASES:
                require(Path(os.path.realpath(child)) == RETAINED_ALIASES[child] and
                        bool(getattr(child.lstat(), "st_file_attributes", 0) & 0x400),
                        "Retained test-only junction identity changed")
                children.remove(name)
            else:
                safe_path(child)
        for name in sorted(names):
            path = safe_path(Path(directory) / name)
            result[path.relative_to(root).as_posix()] = path.read_bytes()
    require(sum(map(len, result.values())) <= MAX_BYTES, f"Retained root exceeds 128 MiB: {root}")
    return result


def file_rows(files: dict[str, bytes]) -> list[dict]:
    return [{"relative_path": path, "bytes": len(data), "sha256": digest(data)}
            for path, data in sorted(files.items())]


def binding(path: Path, repo: Path) -> dict:
    data = safe_path(path).read_bytes()
    return {"relative_path": path.relative_to(repo).as_posix(),
            "bytes": len(data), "sha256": digest(data)}


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def canonical_depth_range(triangles: list[dict]) -> tuple[float, float]:
    bounds = [sum(vertex) / math.sqrt(3.0) for triangle in triangles for vertex in triangle["vertices"]]
    lower, upper = min(bounds), max(bounds)
    lo, hi = f32(lower), f32(upper)
    def adjacent(value, upwards):
        bits = struct.unpack("<I", struct.pack("<f", value))[0]
        if upwards:
            bits += 1 if value >= 0 else -1
        elif value > 0:
            bits -= 1
        else:
            bits = 0x80000001 if value == 0 else bits + 1
        return struct.unpack("<f", struct.pack("<I", bits))[0]
    if lo > lower:
        lo = adjacent(lo, False)
    if hi < upper:
        hi = adjacent(hi, True)
    require(lo <= lower < upper <= hi, "Independent depth bounds do not contain XYZ")
    return lo, hi


def normal(vertices: list[list[float]]) -> tuple[float, float, float]:
    a = tuple(vertices[1][i] - vertices[0][i] for i in range(3))
    b = tuple(vertices[2][i] - vertices[0][i] for i in range(3))
    cross = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
    length = math.sqrt(sum(axis*axis for axis in cross))
    require(length > 1e-9, "Independent XYZ normal is degenerate")
    return tuple(axis/length for axis in cross)


def project(vertex: list[float]) -> tuple[float, float]:
    x, y, z = vertex
    return 29.0*(x-y), 14.5*(x+y)-29.0*z


def weights(point, triangle) -> tuple[float, float, float]:
    # Solve the independently formed two-column screen matrix at vertex A.
    a, b, c = triangle
    ex, ey, fx, fy = b[0]-a[0], b[1]-a[1], c[0]-a[0], c[1]-a[1]
    dx, dy = point[0]-a[0], point[1]-a[1]
    determinant = ex*fy-ey*fx
    require(abs(determinant) > 1e-12, "Independent screen triangle is degenerate")
    wb, wc = (dx*fy-dy*fx)/determinant, (ex*dy-ey*dx)/determinant
    return 1.0-wb-wc, wb, wc


def encode_normal(value) -> tuple[int, int]:
    x, y, z = value
    divisor = abs(x)+abs(y)+abs(z)
    x, y, z = x/divisor, y/divisor, z/divisor
    if z < 0:
        old_x, old_y = x, y
        x = (1-abs(old_y)) * (1 if old_x >= 0 else -1)
        y = (1-abs(old_x)) * (1 if old_y >= 0 else -1)
    return round((x*.5+.5)*255), round((y*.5+.5)*255)


def oracle(master: dict, registration: dict, paint: bytes | None = None) -> dict:
    """Full source-order XYZ winner/6-channel oracle; never calls exporter math."""
    width, height = registration["source_size"]
    pivot_x, pivot_y = registration["pivot_px"]
    eppp = f32(master["export"]["export_pixels_per_reference_pixel"])
    triangles = master["triangles"]
    count = width*height
    winner = array("i", [-1])*count
    depth = array("d", [-math.inf])*count
    screens = [[(u*eppp+pivot_x, v*eppp+pivot_y) for u, v in map(project, row["vertices"])]
               for row in triangles]
    work = 0
    # Iterate source triangles with clipped bounds. The scan differs from the
    # exporter implementation; declared epsilon retains the first tied owner.
    for index, (row, screen) in enumerate(zip(triangles, screens)):
        x0, x1 = max(0, math.floor(min(p[0] for p in screen))), min(width, math.ceil(max(p[0] for p in screen)))
        y0, y1 = max(0, math.floor(min(p[1] for p in screen))), min(height, math.ceil(max(p[1] for p in screen)))
        work += max(0, x1-x0)*max(0, y1-y0)
        for y in range(y0, y1):
            for x in range(x0, x1):
                ws = weights((x+.5, y+.5), screen)
                if min(ws) < -1e-10:
                    continue
                xyz = tuple(sum(ws[i]*row["vertices"][i][axis] for i in range(3)) for axis in range(3))
                value = sum(xyz)/math.sqrt(3.0)
                pixel = y*width+x
                if value > depth[pixel]+1e-12:
                    winner[pixel], depth[pixel] = index, value
    lo, hi = canonical_depth_range(triangles)
    channels = {name: bytearray(count*stride) for name, (_, _, stride) in CHANNELS.items()}
    for pixel, index in enumerate(winner):
        if index < 0:
            if paint is not None:
                require(paint[pixel*4+3] == 0, "Painted baseline has uncovered nonzero alpha")
            continue
        row = triangles[index]
        alpha = 255 if paint is None else paint[pixel*4+3]
        start = pixel*4
        channels["albedo"][start:start+4] = bytes(row["albedo"]) if paint is None else paint[start:start+4]
        if not alpha:
            continue
        nx, ny = encode_normal(normal(row["vertices"]))
        channels["normal_material"][start:start+4] = bytes((nx, ny, round(row["roughness"]*255), round(row["ao"]*255)))
        struct.pack_into("<H", channels["camera_depth"], pixel*2, round((depth[pixel]-lo)/(hi-lo)*65535))
        channels["emission_mask"][pixel] = round(row["emission_mask"]*255)
        channels["surface_mask"][start:start+4] = bytes((round(row["wet_mask"]*255), 0, 0, 255))
        channels["caster_mask"][pixel] = round(row["caster_mask"]*(alpha if paint is not None and
                                       registration["caster_alpha_policy"] == "painted-alpha-times-authored-mask" else 255))
    if paint is not None:
        # Transparent painted RGB is preserved too; old albedo is exact raw RGBA.
        channels["albedo"] = bytearray(paint)
    return {"width": width, "height": height, "depth_range": (lo, hi), "screens": screens,
            "winner": winner, "depth": depth, "work": work, "eppp": eppp,
            "channels": {key: bytes(value) for key, value in channels.items()}}


def check_channels(actual: dict[str, bytes], expected: dict) -> None:
    for name in CHANNELS:
        require(actual[name] == expected["channels"][name], f"Independent channel mismatch: {name}")


def crop(repo: Path, key: str) -> tuple[dict, bytes]:
    from PIL import Image
    catalog = json_read(repo/"assets/art/world.json")
    found = [(page, sprite) for page in catalog["pages"] for sprite in page["sprites"] if sprite["id"] == key]
    require(len(found) == 1, "Original sprite binding is ambiguous")
    page, sprite = found[0]
    x, y, width, height = sprite["rect"]
    with Image.open(repo/"assets"/page["path"]) as image:
        require(image.mode == "RGBA", "Original page is not raw straight RGBA")
        paint = image.crop((x, y, x+width, y+height)).tobytes()
    return {"page": page, "sprite": sprite}, paint


def check_palette(repo: Path, item: dict, master: dict, palette: dict) -> dict:
    require(set(palette) == PALETTE_FIELDS and len(PALETTE_FIELDS) == 15, "Palette known fields differ")
    require(palette["schema"] == 1 and palette["kind"] == "ribterra-neutral-palette-v1" and
            palette["status"] == "DRAFT / SOURCE ONLY / NOT_ACCEPTED", "Palette draft identity differs")
    source = item["albedo_source"]
    require(source["provenance"]["author"] == palette["authoring"]["author"], "Authored palette author declaration differs")
    require(palette["draft_notice"]["path"] == source["provenance"]["notice_path"] and
            palette["draft_notice"]["sha256"] == source["provenance"]["notice_sha256"], "Authored notice differs")
    archived_path = repo/palette["geometry_source"]["path"]
    archived_raw = archived_path.read_bytes()
    require(digest(archived_raw) == palette["geometry_source"]["sha256"], "Archived master digest differs")
    archived = json.loads(archived_raw)
    for field in ("coordinate_system", "export", "local_occluders", "sky_shadow_triangles", "probes"):
        require(master[field] == archived[field], f"Authored geometry field changed: {field}")
    require(len(master["triangles"]) == len(archived["triangles"]) == 114, "Draft triangle count differs")
    for old, new in zip(archived["triangles"], master["triangles"]):
        for field in ("id", "vertices", "ao"):
            require(old[field] == new[field], f"Archived XYZ/order/winding/AO differs: {field}")
    target = palette["source_registration_target"]
    require(target["active_registration"] is False and target["caster_alpha_policy"] ==
            "painted-alpha-times-authored-mask", "Archived painted registration was replaced")
    for field in ("kind", "png_sha256", "catalog_entry_sha256", "source_size", "pivot_px", "reference_width"):
        require(target[field] == item["registration"][field], f"Archived registration field differs: {field}")
    require(item["registration"]["caster_alpha_policy"] == "authored-mask" and
            item["master_sha256"] != palette["geometry_source"]["sha256"], "Active neutral identity is not separate")
    materials = {row["id"]: row for row in palette["materials"]}
    assignments = {row["triangle_id"]: row["material_id"] for row in palette["triangle_materials"]}
    require(len(materials) == len(palette["materials"]) == 14 and
            len(assignments) == len(palette["triangle_materials"]) == 114, "Palette assignment count differs")
    require(set(assignments) == {row["id"] for row in master["triangles"]} and
            set(assignments.values()) == set(materials), "Palette is not a complete unique assignment")
    for triangle in master["triangles"]:
        material = materials[assignments[triangle["id"]]]
        require(triangle["albedo"] == material["albedo_srgb"]+[255], "Master/palette RGB differs")
        for field in ("roughness", "emission_mask", "wet_mask", "caster_mask"):
            require(type(material[field]) in (int, float) and math.isfinite(material[field]) and
                    0 <= material[field] <= 1 and triangle[field] == material[field], f"Master/palette scalar differs: {field}")
    for row in palette["source_bindings"]:
        actual = binding(repo/row["path"], repo)
        require(actual == {"relative_path": row["path"], "bytes": row["bytes"], "sha256": row["sha256"]}, "Palette source binding differs")
    return {"triangles": 114, "materials": 14, "numeric_probes": len(master["probes"]),
            "sky_triangles": len(master["sky_shadow_triangles"]), "archived_geometry_unchanged": True}


def verify_probes(master: dict, item: dict, row: dict, expected: dict, actual: dict) -> None:
    requests = {probe["id"]: probe for probe in master["probes"]}
    indices = {triangle["id"]: index for index, triangle in enumerate(master["triangles"])}
    require(len(row["probes"]) == len(requests) and {p["id"] for p in row["probes"]} == set(requests), "Numeric probe inventory differs")
    width, height = expected["width"], expected["height"]
    pivot = item["registration"]["pivot_px"]
    for probe in row["probes"]:
        request = requests[probe["id"]]
        index = indices[request["triangle_id"]]
        triangle = master["triangles"][index]
        require(probe["triangle_id"] == triangle["id"] and probe["triangle_vertices"] == triangle["vertices"], "Probe geometry identity differs")
        target_xyz = [sum(request["barycentric"][i]*triangle["vertices"][i][axis] for i in range(3)) for axis in range(3)]
        target = tuple(value*expected["eppp"]+pivot[i] for i, value in enumerate(project(target_xyz)))
        center = [math.floor(value) for value in target]
        candidates = []
        for radius in range(33):
            for y in range(max(0, center[1]-radius), min(height, center[1]+radius+1)):
                for x in range(max(0, center[0]-radius), min(width, center[0]+radius+1)):
                    pixel = y*width+x
                    if expected["winner"][pixel] != index or actual["albedo"][pixel*4+3] < 64:
                        continue
                    distances = []
                    screen = expected["screens"][index]
                    for edge in range(3):
                        a, b = screen[edge], screen[(edge+1)%3]
                        distances.append(abs((b[0]-a[0])*(a[1]-y-.5)-(a[0]-x-.5)*(b[1]-a[1]))/math.dist(a, b))
                    clearance = min(distances)
                    if clearance >= 8:
                        candidates.append(((x+.5-target[0])**2+(y+.5-target[1])**2, y, x, clearance))
            if candidates:
                break
        require(bool(candidates), "Independent numeric probe has no safe visible texel")
        _, y, x, clearance = min(candidates)
        require(probe["source_pixel"] == [x+.5, y+.5] and abs(probe["boundary_clearance_px"]-clearance) <= 1e-9, "Probe nearest safe texel differs")
        ws = weights((x+.5, y+.5), expected["screens"][index])
        xyz = [sum(ws[i]*triangle["vertices"][i][axis] for i in range(3)) for axis in range(3)]
        require(all(abs(a-b) <= 1e-9 for a, b in zip(xyz, probe["position"])) and
                all(abs(a-b) <= 1e-12 for a, b in zip(normal(triangle["vertices"]), probe["normal"])), "Probe XYZ/normal differs")
        require(probe["albedo"] == list(actual["albedo"][(y*width+x)*4:(y*width+x+1)*4]), "Probe RGBA differs")
        for field in ("roughness", "ao", "emission_mask", "wet_mask"):
            require(probe[field] == triangle[field], f"Probe scalar differs: {field}")


def manifest_check(root: Path, files: dict[str, bytes]) -> dict:
    value = json.loads(files["manifest.json"])
    require(value["schema"] == 1 and len({row["id"] for row in value["assets"]}) == len(value["assets"]), "Manifest IDs/schema differ")
    text = ""
    for row in sorted(value["assets"], key=lambda item: item["id"]):
        data = files[row["path"]]
        require(row["sha256"] == digest(data) and row["bytes"] == len(data), "Manifest payload binding differs")
        for field in ("id", "path", "kind", "sha256", "license", "source", "author", "notice"):
            field_value = row[field]
            text += f"{len(field_value.encode('utf-8'))}:{field_value}"
        text += "\n"
    require(value["content_sha256"] == digest(text.encode("utf-8")), "Manifest canonical content digest differs")
    return {row["id"]: row for row in value["assets"]}


def report_check(repo: Path, output: Path, files: dict[str, bytes]) -> dict:
    report = json.loads(files["material-collection-report.json"])
    require(report["complete"] is True and report["schema"] == 1 and report["item_count"] == 2, "Collection report identity differs")
    require(report["inputs_before"] == report["inputs_after"], "Export mutated inputs")
    for row in report["inputs_after"]:
        require(binding(repo/row["relative_path"], repo) == row, "Report input SHA/bytes differs")
    require(report["exporter_sha256"] == digest((repo/"tools/export_material_master.py").read_bytes()), "Exporter source identity differs")
    require(report["output_inventory_excluding_report"] == file_rows({k: v for k, v in files.items() if k != report["report_filename"]}) and
            report["output_file_count"] == len(files) and report["output_total_bytes"] == sum(map(len, files.values())), "Collection output ledger differs")
    require(report["max_output_bytes"] == MAX_BYTES and report["min_free_bytes"] == MIN_FREE, "Collection declared budgets differ")
    return report


class ProcessMemory(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                *[(name, ctypes.c_size_t) for name in (
                    "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
                    "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage")]]


def peak_working_set(handle=None, allow_unavailable=False) -> int | None:
    require(os.name == "nt", "Actual memory evidence requires Windows GetProcessMemoryInfo")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessMemory), wintypes.DWORD]
    psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
    counters = ProcessMemory()
    counters.cb = ctypes.sizeof(counters)
    available = bool(psapi.GetProcessMemoryInfo(kernel.GetCurrentProcess() if handle is None else wintypes.HANDLE(int(handle)),
                                              ctypes.byref(counters), counters.cb))
    if allow_unavailable and (not available or counters.PeakWorkingSetSize == 0):
        return None
    require(available and counters.PeakWorkingSetSize > 0, "Actual process peak is unavailable")
    return int(counters.PeakWorkingSetSize)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repo", type=Path)
    parser.add_argument("output_root", type=Path)
    parser.add_argument("--collection", default=DEFAULT_DESCRIPTOR)
    parser.add_argument("--baseline-root", type=Path, required=True)
    parser.add_argument("--painted-baseline-root", type=Path, required=True)
    args = parser.parse_args()
    repo, root = safe_path(args.repo), safe_path(args.output_root)
    build = safe_path(repo/"build")
    require(repo == Path(__file__).resolve().parents[1], "Tests must use the authoritative repository")
    require(root.parent == build and not os.path.lexists(root), "Tests require a fresh immediate repository build child")
    require(shutil.disk_usage(build).free >= MIN_FREE+MAX_BYTES, "Tests cannot retain 384 MiB headroom")
    descriptor_path = safe_path(repo/args.collection)
    require(descriptor_path.is_relative_to(repo), "Descriptor must stay inside the repository")
    collection = json_read(descriptor_path)
    require(collection["schema"] == 1 and collection["kind"] == "ribterra-material-collection-v1" and len(collection["items"]) == 2, "Mixed descriptor identity differs")
    require(collection["items"] == sorted(collection["items"], key=lambda row: row["namespace"]), "Descriptor order differs")
    items = {row["legacy_key"]: row for row in collection["items"]}
    require(set(items) == {"building.house.ready", "resident.man.walk.2.1"}, "Mixed production keys differ")
    house, resident = items["building.house.ready"], items["resident.man.walk.2.1"]
    require(house["albedo_source"]["kind"] == MODE and "albedo_source" not in resident, "Explicit/absent modes differ")
    old_descriptor = json_read(repo/OLD_DESCRIPTOR)
    require(resident == next(row for row in old_descriptor["items"] if row["legacy_key"] == resident["legacy_key"]), "Painted resident item changed")
    palette = json_read(repo/house["albedo_source"]["palette_path"])
    master = json_read(repo/house["master_path"])
    palette_evidence = check_palette(repo, house, master, palette)
    baselines = [safe_path(args.baseline_root), safe_path(args.painted_baseline_root)]
    require(all(path.is_relative_to(build) and path.is_dir() for path in baselines), "Retained baselines must be existing repository build roots")
    baseline_files, painted_files = [tree(path) for path in baselines]
    calibration_metadata = json.loads(baseline_files[CALIBRATION_METADATA_PATH])
    require(calibration_metadata["exporter_sha256"] == CALIBRATION_EXPORTER_SHA and
            calibration_metadata["master_sha256"] == CALIBRATION_MASTER_SHA, "Retained prechange calibration identity differs")
    exporter = repo/"tools/export_material_master.py"
    input_paths = {Path(__file__), exporter, descriptor_path, repo/OLD_DESCRIPTOR,
                   repo/"assets/art/world.json", repo/"content/asset_sources.json", repo/"art-source/engine-calibration/material-master.json"}
    for item in collection["items"]:
        input_paths |= {repo/item["master_path"], repo/item["provenance"]["notice_path"]}
        input_paths |= {repo/row["path"] for row in item["references"]}
        info, _ = crop(repo, item["legacy_key"])
        input_paths.add(repo/"assets"/info["page"]["path"])
    input_paths |= {repo/house["albedo_source"]["palette_path"], repo/house["albedo_source"]["provenance"]["notice_path"],
                    repo/"art-source/production-materials-v1/house-neutral-draft/PROVENANCE_UA.md"}
    input_paths |= {repo/row["path"] for row in palette["source_bindings"]+palette["references"]}
    input_paths |= {repo/row["relative_path"] for row in json.loads(painted_files["material-collection-report.json"])["inputs_before"]}
    input_paths |= {path for baseline in baselines for path in baseline.rglob("*") if path.is_file()}
    before = [binding(path, repo) for path in sorted(input_paths)]
    os.mkdir(root)
    commands, measured, negatives, outputs = [], [], [], []
    def sibling(label: str) -> Path:
        path = safe_path(build/(root.name+"-"+label))
        require(not os.path.lexists(path), f"Retained output already exists: {path}")
        outputs.append(path)
        return path
    def invoke(input_path: Path, output: Path, label: str, collection_mode=True, diagnostic=None) -> None:
        argv = [sys.executable, "-B", str(exporter), str(input_path), str(output)]
        if collection_mode:
            argv.append("--collection")
        start = time.perf_counter()
        process = subprocess.Popen(argv, cwd=repo, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        observed_peak, samples = 0, 0
        try:
            while True:
                sample = peak_working_set(process._handle, allow_unavailable=True)
                if sample is not None:
                    observed_peak, samples = max(observed_peak, sample), samples+1
                remaining = 180-(time.perf_counter()-start)
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(argv, 180)
                try:
                    stdout, stderr = process.communicate(timeout=min(.05, remaining))
                    break
                except subprocess.TimeoutExpired:
                    continue
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()
            write(root/"logs"/(label+".stdout.txt"), stdout)
            write(root/"logs"/(label+".stderr.txt"), stderr)
            raise AssertionError(f"Bounded exporter timed out: {label}") from None
        elapsed = (time.perf_counter()-start)*1000
        final_peak = peak_working_set(process._handle, allow_unavailable=True)
        if final_peak is not None:
            observed_peak, samples = max(observed_peak, final_peak), samples+1
        require(observed_peak > 0 and samples > 0, "No actual Windows child memory counter was observed")
        write(root/"logs"/(label+".stdout.txt"), stdout)
        write(root/"logs"/(label+".stderr.txt"), stderr)
        commands.append({"id": label, "argv": argv, "exit": process.returncode,
                         "stdout_sha256": digest(stdout), "stderr_sha256": digest(stderr)})
        measured.append({"id": label, "elapsed_ms": elapsed, "observed_python_peak_working_set_bytes": observed_peak,
                         "counter_samples": samples, "after_exit_counter_available": final_peak is not None,
                         "scope": "Full-lifetime counter if post-exit available; otherwise peak at last successful live observation"})
        require(process.returncode == (2 if diagnostic else 0), f"Exporter exit differs for {label}: {stderr.decode('utf-8', errors='replace')}")
        if diagnostic:
            require(diagnostic in stderr.decode("utf-8", errors="strict"), f"Rejection hit another validation for {label}: {stderr!r}")
        require(shutil.disk_usage(build).free >= MIN_FREE, "Export ended below 384 MiB headroom")
        tree(root)
    # Retained same-input, not regenerated golden, compatibility controls.
    calibration_output = sibling("calibration")
    invoke(repo/"art-source/engine-calibration/material-master.json", calibration_output, "calibration", False)
    default_files = tree(calibration_output)
    require(set(default_files) == set(baseline_files), "Calibration output inventory changed")
    for path in default_files:
        if path not in {CALIBRATION_METADATA_PATH, "manifest.json"}:
            require(default_files[path] == baseline_files[path], f"Default non-report bytes changed: {path}")
    new_metadata = json.loads(default_files[CALIBRATION_METADATA_PATH])
    require(new_metadata.pop("exporter_sha256") == digest(exporter.read_bytes()), "Default exporter SHA is stale")
    old_metadata = copy.deepcopy(calibration_metadata)
    old_metadata.pop("exporter_sha256")
    require(new_metadata == old_metadata, "Default camera/settings/numeric probe semantics changed")
    default_manifest = manifest_check(calibration_output, default_files)
    baseline_manifest = manifest_check(baselines[0], baseline_files)
    for identity in default_manifest:
        a, b = copy.deepcopy(default_manifest[identity]), copy.deepcopy(baseline_manifest[identity])
        if a["path"] == CALIBRATION_METADATA_PATH:
            a.pop("sha256"); b.pop("sha256")
            a.pop("bytes"); b.pop("bytes")
        require(a == b, "Default manifest changed beyond truthful calibration report binding")
    painted_output = sibling("painted-compatible")
    invoke(repo/OLD_DESCRIPTOR, painted_output, "painted-compatible")
    fresh_painted = tree(painted_output)
    require(set(fresh_painted) == set(painted_files), "Painted absent-mode inventory changed")
    for path in fresh_painted:
        if path != "material-collection-report.json":
            require(fresh_painted[path] == painted_files[path], f"Painted absent-mode bytes changed: {path}")
    fresh_report = report_check(repo, painted_output, fresh_painted)
    old_report = json.loads(painted_files["material-collection-report.json"])
    for report in (fresh_report, old_report):
        report.pop("exporter_sha256")
        report.pop("output_total_bytes")
        for key in ("inputs_before", "inputs_after"):
            report[key] = [row for row in report[key] if row["relative_path"] != "tools/export_material_master.py"]
    require(fresh_report == old_report, "Painted report changed beyond declared exporter source/size binding")
    export_a, export_b = sibling("export-a"), sibling("export-b")
    invoke(descriptor_path, export_a, "export-a")
    invoke(descriptor_path, export_b, "export-b")
    files_a, files_b = tree(export_a), tree(export_b)
    require(files_a == files_b, "Two neutral mixed exports are not byte-exact including reports")
    report_check(repo, export_a, files_a)
    manifest = manifest_check(export_a, files_a)
    expected_files = {"art/world.json", "art/material-collection.json", "manifest.json", "NOTICE.txt", "material-collection-report.json"}
    expected_ids = {"art.world", "material.collection.metadata"}
    for item in collection["items"]:
        expected_files.add(item["provenance"]["notice_path"])
        for name, (filename, _, _) in CHANNELS.items():
            expected_files.add(f"art/materials/{item['namespace']}/{filename}")
            expected_ids.add(f"{item['namespace']}.texture.{name}")
        if "albedo_source" in item:
            expected_files |= {item["albedo_source"]["palette_path"], item["albedo_source"]["provenance"]["notice_path"]}
            expected_ids.add(item["namespace"]+".palette")
    require(set(files_a) == expected_files and len(files_a) == 20 and set(manifest) == expected_ids and len(manifest) == 15,
            "Neutral collection contains missing/extra runtime payloads")
    world, metadata = json.loads(files_a["art/world.json"]), json.loads(files_a["art/material-collection.json"])
    require(world["schema"] == 2 and world["pages"] == [] and len(world["textures"]) == 12 and
            len(world["materials"]) == len(world["lit_sprites"]) == metadata["item_count"] == 2, "Mixed schema2 counts differ")
    require(metadata["reference_fidelity"] == "NOT_ACCEPTED" and metadata["production_master_acceptance"] == "OPEN" and
            metadata["normal_startup"] == metadata["full_engine"] == "OPEN", "CPU output claims unsupported acceptance")
    rows_by_key = {row["legacy_key"]: row for row in metadata["items"]}
    item_evidence, oracle_negative_controls = [], []
    house_expected, house_actual, house_paint = None, None, None
    for item in collection["items"]:
        source_master = json_read(repo/item["master_path"])
        original_info, paint = crop(repo, item["legacy_key"])
        neutral = "albedo_source" in item
        expected = oracle(source_master, item["registration"], None if neutral else paint)
        row = rows_by_key[item["legacy_key"]]
        page, original_sprite = original_info["page"], original_info["sprite"]
        source_metadata = json_read(repo/"content/asset_sources.json")["overrides"][page["path"]]
        source_paths = {"assets/"+page["path"], item["provenance"]["notice_path"]}
        source_paths |= {reference["path"] for reference in item["references"]}
        source_paths |= {image["path"] for image in source_metadata.get("source_images", [])}
        if "prompt_file" in source_metadata:
            source_paths.add(source_metadata["prompt_file"])
        expected_registration = {"page_path": page["path"], "rect": original_sprite["rect"],
            "pivot": original_sprite["pivot"], "pivot_px": [f32(f32(pivot)*size) for pivot, size in
                zip(original_sprite["pivot"], original_sprite["rect"][2:])],
            "source_size": original_sprite["rect"][2:],
            "reference_width": f32(original_sprite.get("reference_width", original_sprite["rect"][2])),
            "png_sha256": digest((repo/"assets"/page["path"]).read_bytes()), "decoded_crop_sha256": digest(paint),
            "catalog_entry_sha256": digest(canonical({"page": {"path": page["path"]}, "sprite": original_sprite})),
            "caster_alpha_policy": item["registration"]["caster_alpha_policy"], "asset_source": source_metadata,
            "provenance": item["provenance"], "references": item["references"],
            "source_bindings": [binding(repo/path, repo) for path in sorted(source_paths)]}
        require(row["registration"] == expected_registration and row["master_sha256"] == digest((repo/item["master_path"]).read_bytes()) and
                row["geometry_provenance"] == item["geometry_provenance"], "Independently derived source/registration/provenance differs")
        actual = {name: files_a[f"art/materials/{item['namespace']}/{filename}"] for name, (filename, _, _) in CHANNELS.items()}
        check_channels(actual, expected)
        verify_probes(source_master, item, row, expected, actual)
        require(row["triangle_count"] == len(source_master["triangles"]) and row["depth_range"] == list(expected["depth_range"]) and
                row["export_pixels_per_reference_pixel"] == expected["eppp"], "Derived geometry metadata differs")
        sprite = next(sprite for sprite in world["lit_sprites"] if sprite["id"] == "dynamic."+item["legacy_key"])
        require(sprite["source_size"] == item["registration"]["source_size"] and sprite["pivot_px"] == item["registration"]["pivot_px"] and
                sprite["depth_range"] == row["depth_range"] and sprite["export_pixels_per_reference_pixel"] == expected["eppp"], "Shared sprite extent/pivot/depth differs")
        for name, (_, texture_format, stride) in CHANNELS.items():
            texture = next(texture for texture in world["textures"] if texture["id"] == f"{item['namespace']}.texture.{name}")
            require(texture["format"] == texture_format and texture["layout"] == "tight_le" and texture["mip_levels"] == 1 and
                    texture["width"] == expected["width"] and texture["height"] == expected["height"] and
                    texture["bytes"] == expected["width"]*expected["height"]*stride and texture["sha256"] == digest(actual[name]) and
                    manifest[texture["id"]]["sha256"] == texture["sha256"], "Six-channel descriptor binding differs")
        source_record = {key: row[key] for key in ("master_sha256", "registration", "geometry_provenance")}
        if neutral:
            source_record.update(albedo_source=row["albedo_source"], generated_albedo_sha256=row["generated_albedo_sha256"])
            require(row["albedo_source"] == item["albedo_source"] and row["generated_albedo_sha256"] == digest(actual["albedo"]), "Generated albedo source identity differs")
        require("source_record" not in row and row["source_hash"] == digest(canonical(source_record)), "Flattened source_record identity differs")
        material = next(material for material in world["materials"] if material["id"] == row["material_id"])
        require(material["source_hash"] == row["source_hash"] and material["emission"] == source_master["emission_radiance"] and
                material["family"] == "DynamicSprite25D" and material["alpha"] == "straight" and material["camera"] == "ribterra-affine-v1",
                "Material logical source/lighting binding differs")
        generated = [alpha != 0 for alpha in actual["albedo"][3::4]]
        original = [alpha != 0 for alpha in paint[3::4]]
        overlap = sum(a and b for a, b in zip(original, generated))
        census = {"original_alpha_samples": sum(original), "generated_alpha_samples": sum(generated),
                  "overlap_alpha_samples": overlap, "omitted_original_alpha_samples": sum(original)-overlap,
                  "generated_only_alpha_samples": sum(generated)-overlap}
        require(row["supported_alpha_samples"] == sum(generated), "Supported-alpha census differs")
        if neutral:
            require(row["coverage"] == census and row["uncovered_alpha_samples"] == census["omitted_original_alpha_samples"], "Neutral actual coverage census differs")
            gap = palette["legacy_alpha_gap"]
            require(gap["source_nonzero_alpha_pixels"] == census["original_alpha_samples"] and
                    gap["source_pixels_supported_by_old_geometry"] == overlap and
                    gap["source_pixels_unsupported_by_old_geometry"] == census["omitted_original_alpha_samples"],
                    "Independently measured preserved-geometry paint gap differs from archived data")
            require(set(actual["albedo"][3::4]) <= {0, 255} and census["generated_alpha_samples"] > 0 and
                    census["omitted_original_alpha_samples"] > 0, "Neutral alpha hides the unsupported original paint gap")
            require(sum(actual["albedo"][p*4:p*4+3] != paint[p*4:p*4+3] for p, visible in enumerate(generated) if visible) > 1000, "Neutral RGB was copied from baked paint")
            house_expected, house_actual, house_paint = expected, actual, paint
            for name in CHANNELS:
                corrupt = dict(actual)
                data = bytearray(corrupt[name])
                data[0] ^= 1
                corrupt[name] = bytes(data)
                try:
                    check_channels(corrupt, expected)
                except AssertionError as error:
                    require(str(error) == f"Independent channel mismatch: {name}", "Oracle corruption hit another check")
                else:
                    raise AssertionError(f"Independent oracle accepted corrupt {name}")
                oracle_negative_controls.append(name)
        else:
            require(actual["albedo"] == paint and row["uncovered_alpha_samples"] == 0 and "albedo_source" not in row and
                    "generated_albedo_sha256" not in row, "Absent mode changed painted identity/alpha")
        item_evidence.append({"legacy_key": item["legacy_key"], "mode": MODE if neutral else "original-painted",
                              "texels": expected["width"]*expected["height"], "triangles": len(source_master["triangles"]),
                              "bounded_triangle_pixel_checks": expected["work"], "coverage": census,
                              "channels": {key: digest(value) for key, value in actual.items()}, "probes": len(row["probes"])})
    source = house["albedo_source"]
    require(files_a[source["palette_path"]] == (repo/source["palette_path"]).read_bytes() and
            files_a[source["provenance"]["notice_path"]] == (repo/source["provenance"]["notice_path"]).read_bytes(), "Delivered authored palette/notice bytes differ")
    palette_row = manifest[house["namespace"]+".palette"]
    require(palette_row["path"] == source["palette_path"] and palette_row["kind"] == "data" and
            palette_row["sha256"] == source["palette_sha256"] and palette_row["notice"] == source["provenance"]["notice_path"], "Delivered palette registry differs")
    require(palette_row["source"] == descriptor_path.relative_to(repo).as_posix(), "Delivered palette descriptor source differs")
    for field in ("license", "author"):
        require(palette_row[field] == source["provenance"][field], "Delivered palette provenance differs")
    for notice in (house["provenance"]["notice_path"], source["provenance"]["notice_path"]):
        require(files_a[notice] == (repo/notice).read_bytes() and notice in files_a["NOTICE.txt"].decode("utf-8"), "Original/authored notice delivery differs")
    require(house["master_path"] not in files_a, "Runtime collection packages the XYZ master")
    # Public module API direct controls use only the agreed explicit parsed marker.
    spec = importlib.util.spec_from_file_location("neutral_exporter_under_test", exporter)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    # Exercise the pure payload merge without creating any retained output. Each
    # rejected candidate begins with a valid entry, so partial updates are visible.
    collision_diagnostic = "Authored collection path collides with another output"
    base_payload = {"art/world.json": b"original catalog", "NOTICE.txt": b"original notice",
                    "textures/base.rtex": b"original texture"}
    positive_payload = base_payload.copy()
    positive_authored = {"neutral/palette.json": b"authored palette", "neutral/notice.txt": b"authored notice"}
    module.merge_authored_payload(positive_payload, positive_authored)
    require(positive_payload == {**base_payload, **positive_authored} and
            positive_authored == {"neutral/palette.json": b"authored palette", "neutral/notice.txt": b"authored notice"},
            "Pure authored payload merge did not preserve existing/new bytes")
    payload_collision_controls = [{"id": "positive-disjoint", "variant_count": 1,
                                   "merged_exact": True, "authored_input_exact": True}]
    collision_groups = (
        ("existing-exact", (("NOTICE.txt",),)),
        ("existing-casefold", (("notice.TXT",),)),
        ("reserved-report", (("material-collection-report.json",),
                             ("Material-Collection-Report.JSON",),
                             ("material-collection-report.json/child.txt",))),
        ("existing-notice-child", (("NOTICE.txt/child.txt",),)),
        ("existing-catalog-ancestor", (("art",),)),
        ("existing-catalog-casefold", (("ART/WORLD.JSON",),)),
        ("new-casefold-alias", (("neutral/alias.txt", "NEUTRAL/ALIAS.TXT"),)),
        ("new-file-before-child", (("neutral/node", "neutral/node/child.txt"),)),
        ("new-child-before-file", (("neutral/node/child.txt", "neutral/node"),)),
    )
    for label, variants in collision_groups:
        for paths in variants:
            candidate_payload = base_payload.copy()
            candidate_authored = {"neutral/valid-pending.txt": b"valid pending payload"}
            candidate_authored.update((path, b"collision payload") for path in paths)
            original_payload, original_authored = candidate_payload.copy(), candidate_authored.copy()
            try:
                module.merge_authored_payload(candidate_payload, candidate_authored)
            except module.ExportError as error:
                require(str(error) == collision_diagnostic, f"Payload collision {label} rejected for an unintended reason")
            else:
                raise AssertionError(f"Payload collision {label} was accepted")
            require(candidate_payload == original_payload and candidate_authored == original_authored,
                    f"Payload collision {label} partially published or mutated input bytes")
        payload_collision_controls.append({"id": label, "variant_count": len(variants),
                                           "diagnostic": collision_diagnostic, "original_exact": True,
                                           "pending_unpublished": True, "authored_input_exact": True})
    def module_raster(value):
        _, parsed = module.parse_master(canonical(value))
        parsed["painted_registration"] = {**house["registration"], "rgba": house_paint}
        parsed["albedo_source_kind"] = MODE
        return parsed, module.rasterize(parsed)
    parsed, direct = module_raster(master)
    check_channels({key: direct[key] for key in CHANNELS}, house_expected)
    require(module.build_probes(parsed, direct) == rows_by_key[house["legacy_key"]]["probes"], "Module/CLI numeric probe outputs differ")
    rgb_master = copy.deepcopy(master)
    for triangle in rgb_master["triangles"]:
        triangle["albedo"][0] = (triangle["albedo"][0]+37) % 256
    _, rgb_raster = module_raster(rgb_master)
    require(rgb_raster["albedo"] != direct["albedo"] and all(rgb_raster[key] == direct[key] for key in CHANNELS if key != "albedo"), "RGB-only direct control changed XYZ channels")
    geometry_master = copy.deepcopy(master)
    visible_index = house_expected["winner"][next(p for p, index in enumerate(house_expected["winner"]) if index >= 0)]
    geometry_master["triangles"][visible_index]["vertices"][0][2] += .075
    _, geometry_raster = module_raster(geometry_master)
    normal_changes = sum(a != b for a, b in zip(geometry_raster["normal_material"], direct["normal_material"]))
    depth_changes = sum(a != b for a, b in zip(geometry_raster["camera_depth"], direct["camera_depth"]))
    require(normal_changes > 0 and depth_changes > 0, "XYZ direct mutation has no normal/depth response")
    for label, value, raster in (("rgb-only", rgb_master, rgb_raster), ("xyz-only", geometry_master, geometry_raster)):
        check_channels({key: raster[key] for key in CHANNELS}, oracle(value, house["registration"]))
        write(root/"direct-controls"/(label+"-master.json"), canonical(value))
        for key, (filename, _, _) in CHANNELS.items():
            write(root/"direct-controls"/label/filename, raster[key])
    assignments = {row["triangle_id"]: row["material_id"] for row in palette["triangle_materials"]}
    lantern_indices = {i for i, triangle in enumerate(master["triangles"]) if assignments[triangle["id"]] == "metal.lantern_housing"}
    tie_pixels = [p for p, index in enumerate(house_expected["winner"]) if index in lantern_indices]
    require(bool(tie_pixels) and all(house_actual["surface_mask"][p*4] == 76 for p in tie_pixels), "Actual wet0.30 channel must use binary64 ties-to-even76")
    # Each semantic palette mutation is rebound to its own retained input, so a
    # generic stale digest cannot masquerade as the intended negative validation.
    def negative(label, diagnostic, mutate=lambda value: None, palette_change=None, master_change=None,
                 palette_bytes=None, authored_notice=None):
        candidate = copy.deepcopy(collection)
        target = next(row for row in candidate["items"] if row["legacy_key"] == house["legacy_key"])
        if master_change:
            value = copy.deepcopy(master)
            master_change(value)
            path = root/"fixtures"/(label+"-master.json")
            write(path, canonical(value))
            target.update(master_path=path.relative_to(repo).as_posix(), master_sha256=digest(path.read_bytes()))
        if palette_change or palette_bytes is not None:
            value = copy.deepcopy(palette)
            if palette_change:
                palette_change(value)
            data = canonical(value) if palette_bytes is None else palette_bytes
            path = root/"fixtures"/(label+"-palette.json")
            write(path, data)
            target["albedo_source"].update(palette_path=path.relative_to(repo).as_posix(), palette_sha256=digest(data))
        if authored_notice is not None:
            path = root/"fixtures"/(label+"-NOTICE.txt")
            write(path, authored_notice)
            target["albedo_source"]["provenance"].update(notice_path=path.relative_to(repo).as_posix(), notice_sha256=digest(authored_notice))
            value = copy.deepcopy(palette)
            value["draft_notice"] = {"path": path.relative_to(repo).as_posix(), "sha256": digest(authored_notice)}
            palette_path = root/"fixtures"/(label+"-palette.json")
            write(palette_path, canonical(value))
            target["albedo_source"].update(palette_path=palette_path.relative_to(repo).as_posix(), palette_sha256=digest(palette_path.read_bytes()))
        mutate(target)
        path = root/"fixtures"/(label+"-collection.json")
        write(path, canonical(candidate))
        inputs = {p: p.read_bytes() for p in (path.parent).glob(label+"-*") if p.is_file()}
        output = sibling("negative-"+label)
        invoke(path, output, "negative-"+label, diagnostic=diagnostic)
        require(not os.path.lexists(output), f"Rejected {label} published output")
        require(all(p.read_bytes() == data for p, data in inputs.items()), "Negative input mutated")
        negatives.append({"id": label, "diagnostic": diagnostic, "exit": 2, "output_absent": True, "inputs_exact": True})
    negative("mode-null", "albedo_source fields", lambda item: item.update(albedo_source=None))
    negative("mode-unknown", "Unsupported albedo_source kind", lambda item: item["albedo_source"].update(kind="paint-derived-normal"))
    negative("mode-extra", "albedo_source fields", lambda item: item["albedo_source"].update(extra=True))
    negative("mode-missing", "albedo_source fields", lambda item: item["albedo_source"].pop("palette_sha256"))
    negative("palette-hash", "Declared input hash mismatch", lambda item: item["albedo_source"].update(palette_sha256="0"*64))
    negative("palette-escape", "confined repository-relative", lambda item: item["albedo_source"].update(palette_path="../outside.json"))
    negative("notice-hash", "Declared input hash mismatch", lambda item: item["albedo_source"]["provenance"].update(notice_sha256="0"*64))
    negative("authored-provenance-extra", "albedo_source.provenance fields", lambda item: item["albedo_source"]["provenance"].update(extra=True))
    negative("caster-painted", "Authored albedo requires authored-mask", lambda item: item["registration"].update(caster_alpha_policy="painted-alpha-times-authored-mask"))
    negative("palette-schema", "Unsupported neutral palette schema/kind", palette_change=lambda value: value.update(schema=2))
    negative("palette-kind", "Unsupported neutral palette schema/kind", palette_change=lambda value: value.update(kind="paint-colour-copy"))
    negative("palette-extra", "neutral palette fields", palette_change=lambda value: value.update(extra=True))
    negative("palette-status", "Neutral palette must retain DRAFT status", palette_change=lambda value: value.update(status="ACCEPTED"))
    negative("palette-duplicate-json-key", "contains duplicate JSON field", palette_bytes=canonical(palette).rstrip()[:-1]+b',"schema":1}\n')
    negative("material-extra", "palette material fields", palette_change=lambda value: value["materials"][0].update(extra=True))
    negative("assignment-extra", "palette triangle assignment fields", palette_change=lambda value: value["triangle_materials"][0].update(extra=True))
    negative("material-duplicate", "Duplicate palette material", palette_change=lambda value: value["materials"].append(copy.deepcopy(value["materials"][0])))
    negative("assignment-duplicate", "Duplicate palette triangle assignment", palette_change=lambda value: value["triangle_materials"].append(copy.deepcopy(value["triangle_materials"][0])))
    negative("assignment-missing", "Palette triangle assignments must cover every master triangle", palette_change=lambda value: value["triangle_materials"].pop())
    negative("assignment-unknown-triangle", "Unknown palette triangle", palette_change=lambda value: value["triangle_materials"][0].update(triangle_id="missing.triangle"))
    negative("assignment-unknown-material", "Unknown palette material", palette_change=lambda value: value["triangle_materials"][0].update(material_id="missing.material"))
    def unused(value):
        value["materials"].append({**value["materials"][0], "id": "unused.material"})
    negative("material-unused", "Unused palette material", palette_change=unused)
    for label, bad_value in (("bool", True), ("negative", -.01), ("above-one", 1.01), ("text", "0.5")):
        negative("scalar-"+label, "palette material scalar", palette_change=lambda value, bad=bad_value: value["materials"][0].update(roughness=bad))
    for field in ("emission_mask", "wet_mask", "caster_mask"):
        negative("scalar-"+field+"-bool", "palette material scalar", palette_change=lambda value, key=field: value["materials"][0].update({key:True}))
        negative("scalar-"+field+"-above-one", "palette material scalar", palette_change=lambda value, key=field: value["materials"][0].update({key:1.01}))
    # JSON permits NaN in some parsers; a retained raw token must reject before
    # scalar publication. Canonical mutation files themselves disallow nonfinite.
    for token in (b"NaN", b"Infinity", b"-Infinity"):
        raw = canonical(palette).replace(b'"roughness":0.9', b'"roughness":'+token, 1)
        require(raw != canonical(palette), "Nonfinite fixture mutation did not change bytes")
        negative("scalar-"+token.decode().replace("-", "minus-"), "Nonfinite JSON constant", palette_bytes=raw)
    for label, bad_value in (("bool", True), ("fractional", 1.5), ("negative", -1), ("above-byte", 256)):
        negative("rgb-"+label, "palette albedo_srgb byte", palette_change=lambda value, bad=bad_value: value["materials"][0]["albedo_srgb"].__setitem__(0, bad))
    negative("rgb-length", "palette albedo_srgb", palette_change=lambda value: value["materials"][0]["albedo_srgb"].append(255))
    negative("master-rgb-mismatch", "Master/palette albedo mismatch", master_change=lambda value: value["triangles"][0]["albedo"].__setitem__(0, 19))
    negative("master-scalar-mismatch", "Master/palette scalar mismatch", master_change=lambda value: value["triangles"][0].update(wet_mask=.125))
    for alpha in (0, 128, 254):
        negative("master-alpha-"+str(alpha), "Authored albedo requires opaque triangle alpha255", master_change=lambda value, a=alpha: value["triangles"][0]["albedo"].__setitem__(3, a))
    negative("palette-over-512k", "Neutral palette exceeds 512 KiB", palette_bytes=canonical(palette)+b" "*(512*1024))
    negative("notice-over-128k", "Authored notice exceeds 128 KiB", authored_notice=b"provisional notice\n"*(128*1024//19+1))
    negative("materials-over-256", "palette materials count", palette_change=lambda value: value.update(materials=[{**value["materials"][0], "id":f"material.{i}"} for i in range(257)]))
    negative("assignments-over-10k", "palette triangle_materials count", palette_change=lambda value: value.update(triangle_materials=[{"triangle_id":"t", "material_id":"m"}]*10001))
    negative("master-triangles-over-10k", "triangle count exceeds 10000", master_change=lambda value: value.update(triangles=[value["triangles"][0]]*10001))
    negative("references-over-64", "palette references count", palette_change=lambda value: value.update(references=[value["references"][0]]*65))
    negative("bindings-over-64", "palette source_bindings count", palette_change=lambda value: value.update(source_bindings=[value["source_bindings"][0]]*65))
    negative("palette-string-over-4096", "palette string exceeds 4096 UTF-8 bytes", palette_change=lambda value: value["authoring"].update(author="x"*4097))
    negative("provenance-over-512", "albedo_source.provenance text exceeds 512 UTF-8 bytes", lambda item: item["albedo_source"]["provenance"].update(author="x"*513))
    negative("palette-path-over-1024", "portable path exceeds 1024 UTF-8 bytes", lambda item: item["albedo_source"].update(palette_path="a/"*513+"palette.json"))
    negative("binding-hash", "Declared input hash mismatch", palette_change=lambda value: value["source_bindings"][0].update(sha256="0"*64))
    negative("binding-extra", "palette source binding fields", palette_change=lambda value: value["source_bindings"][0].update(extra=True))
    negative("binding-duplicate", "Duplicate palette source binding", palette_change=lambda value: value["source_bindings"].append(copy.deepcopy(value["source_bindings"][0])))
    negative("reference-extra", "palette reference fields", palette_change=lambda value: value["references"][0].update(extra=True))
    negative("reference-hash", "Declared input hash mismatch", palette_change=lambda value: value["references"][0].update(sha256="0"*64))
    negative("registration-pivot", "extent/pivot/reference width", lambda item: item["registration"].update(pivot_px=[0, 0]))
    negative("registration-width", "extent/pivot/reference width", lambda item: item["registration"].update(reference_width=1))
    negative("registration-png", "Declared input hash mismatch", lambda item: item["registration"].update(png_sha256="0"*64))
    # Only this fresh test-owned alias is created; it is retained permanently.
    junction = root/"retained-reparse-fixtures"
    process = subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", str(junction), str(root/"fixtures")], capture_output=True, check=False)
    write(root/"logs/junction.stdout.txt", process.stdout)
    write(root/"logs/junction.stderr.txt", process.stderr)
    require(process.returncode == 0, "Retained reparse control could not be created")
    RETAINED_ALIASES[junction] = root/"fixtures"
    neutral_palette_copy = root/"fixtures/reparse-palette.json"
    write(neutral_palette_copy, canonical(palette))
    negative("reparse-palette", "Reparse/symlink path is forbidden", lambda item: item["albedo_source"].update(
        palette_path=(junction/neutral_palette_copy.name).relative_to(repo).as_posix(), palette_sha256=digest(neutral_palette_copy.read_bytes())))
    existing = sibling("existing-output")
    os.mkdir(existing)
    write(existing/"sentinel.txt", b"preserve existing output\n")
    invoke(descriptor_path, existing, "existing-output", diagnostic="fresh immediate repository build child")
    require(tree(existing) == {"sentinel.txt": b"preserve existing output\n"}, "Existing output was changed")
    nested = root/"nested-output"
    invoke(descriptor_path, nested, "nested-output", diagnostic="fresh immediate repository build child")
    require(not os.path.lexists(nested), "Nested output was created")
    negatives.extend({"id": label, "diagnostic": "fresh immediate repository build child", "exit": 2,
                      "output_absent": label != "existing-output", "inputs_exact": True} for label in ("existing-output", "nested-output"))
    after = [binding(path, repo) for path in sorted(input_paths)]
    require(before == after, "Original/protected/baseline input bytes changed")
    require(tree(baselines[0]) == baseline_files and tree(baselines[1]) == painted_files, "Retained baseline payload changed")
    require(shutil.disk_usage(build).free >= MIN_FREE, "Tests ended below 384 MiB headroom")
    evidence = {"schema": 1, "complete": True, "deterministic": {
        "scope": "CPU provisional opaque house XYZ/palette plus unchanged diagnostic painted resident",
        "collection_repeat_exact": True, "painted_absent_non_report_byte_exact": True,
        "default_non_report_byte_exact": True, "default_camera_settings_numeric_probes_exact": True,
        "palette": palette_evidence, "items": item_evidence, "collection_inventory": file_rows(files_a),
        "direct_controls": {"rgb_only_geometry_channels_exact": True, "xyz_normal_changed_bytes": normal_changes,
                            "xyz_depth_changed_bytes": depth_changes, "wet0_30_encoded": 76, "wet_tie_qualified_pixels": len(tie_pixels)},
        "oracle_negative_controls": oracle_negative_controls, "negative_controls": negatives,
        "payload_collision_controls": payload_collision_controls,
        "protected_before": before, "protected_after": after, "commands": commands,
        "output_roots": [str(path) for path in outputs if path.exists()],
        "retained_reparse_path": str(junction), "max_root_bytes": MAX_BYTES, "min_free_bytes": MIN_FREE,
        "reference_fidelity": "NOT_ACCEPTED", "neutral_physical_material_acceptance": "OPEN",
        "production_eight_masters": "OPEN", "gpu": "OPEN", "normal_main": "OPEN", "full_engine": "OPEN"},
        "measured": {"export_python_processes": measured, "test_python_peak_working_set_bytes": peak_working_set(),
                     "memory_scope": "Actual Windows counters; child last-observation scope is explicit; measurements separate from repeat bytes"}}
    write(root/"neutral-material-export-test-report.json", canonical(evidence))
    tree(root)
    print(json.dumps({"complete": True, "negative_controls": len(negatives), "report": str(root/"neutral-material-export-test-report.json")}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
