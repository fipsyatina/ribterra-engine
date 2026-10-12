#!/usr/bin/env python3
"""Independent read-only reader of all six explicit textured diagnostic repeats.

No files are created or repaired. Source, explicit SDK, actual command/object,
asset, captured-pixel and state witnesses are checked from retained inputs.
No production material, reference, main, whole-engine or FPS acceptance follows.
"""
from __future__ import annotations
import argparse
import base64
import copy
import hashlib
import json
import math
import os
import stat
import struct
import sys
from collections import Counter
from pathlib import Path
sys.dont_write_bytecode = True
REPOSITORY = Path(r"C:\Users\fipst\Desktop\ribterra")
# These are pure readers/math; importing them neither runs their CLI nor exports.
sys.path.insert(0, str(REPOSITORY / "tests"))
from gameview_cumulative_evidence_tests import bound_rows, digest, hash_row, load, require
from gameview_ui_evidence_tests import verify_ui_kit, STRICT_FLAGS, TRANSLATION_UNITS as UI_TRANSLATION_UNITS
from gameview_material_semantics_tests import (canonical, f32, close_values, manifest_content,
    original_sprite, projected_ground, verify_legacy_quad, lighting)

TRANSLATION_UNITS = (UI_TRANSLATION_UNITS - {"tools/gameview_ui_probe.cpp"}) | {"tools/gameview_textured_material_probe.cpp"}
MAXIMUM = 128 * 1024**2
MINIMUM_FREE = 384 * 1024**2
KEYS = ("building.house.ready", "resident.man.walk.2.1")
CAPTURES = ("textured-menu-first", "textured-body-default", "textured-body-default-house-removed", "textured-original-default",
    "textured-neutral-default", "textured-painted-default", "textured-outline-default", "textured-body-min", "textured-outline-min",
    "textured-body-max", "textured-outline-max", "textured-mixed-wet", "textured-two-houses-selected")

SAVES = tuple(f"textured-material-{name}.save" for name in ("day", "night", "wet", "variants", "winter", "two-houses"))
API_IDS = ("unknown-id-alpha-zero", "invalid-pass-alpha-zero", "invalid-content-alpha-zero", "radius-negative",
    "radius-nan", "radius-infinite", "zoom-zero", "zoom-nan", "scale-zero", "ground-nan", "prefix-short",
    "prefix-long", "authored-prefix-not-synthesized", "alpha-zero", "radius-zero")
COLD_CASES = {
    "cold-source": ("reference_width", "decoded_crop_hash", "legacy_key", "albedo_byte", "neutral-extra",
        "authored-provenance", "coverage", "generated-sha", "source-missing", "source-sha", "uv-range", "material-assignment"),
    "cold-limits": ("schema", "palette-declared-oversize", "palette-actual-oversize", "palette-reread-oversize", "notice-reparse",
        "root-reparse", "palette-internal-reparse", "source-dimensions", "source-bytes", "source-physical-oversize", "source-role-count", "collection-role-count")}
PROFILE_IDS = ("default-profile-textured-mode", "old-profile-textured-mode", "unknown-read-profile")
COLD_DIAGNOSTICS = {
    "default-profile-textured-mode": "explicit MaterialCollectionTextured read profile",
    "old-profile-textured-mode": "explicit MaterialCollectionTextured read profile", "unknown-read-profile": "Unknown asset catalog read profile",
    "schema": "Textured material integer exceeds its bounds", "reference_width": "Registered source crop/pivot/reference width/catalog hash",
    "decoded_crop_hash": "Registered decoded paint hash", "legacy_key": "unique exact supported original legacy key",
    "albedo_byte": "Registered albedo must preserve the exact original cropped RGBA", "neutral-extra": "Textured material object has missing or extra fields",
    "authored-provenance": "Textured generated channel manifest/provenance binding differs", "coverage": "coverage declarations differ",
    "generated-sha": "Generated neutral albedo digest", "source-missing": "Textured material role is unregistered",
    "source-sha": "Asset content hash/size mismatch", "uv-range": "Neutral material coordinate pair is outside its finite bounds",
    "material-assignment": "unknown material", "palette-declared-oversize": "Textured material JSON declared bytes exceed 512 KiB",
    "palette-actual-oversize": "Textured material actual bytes exceed role limit or declared size differs",
    "palette-reread-oversize": "Textured material actual bytes exceed role limit or declared size differs",
    "notice-reparse": "Material catalog rejects", "root-reparse": "Material catalog rejects", "palette-internal-reparse": "Material catalog rejects",
    "source-dimensions": "Textured material integer exceeds its bounds", "source-bytes": "Textured source payload must be exact 3WH",
    "source-physical-oversize": "Textured material actual role bytes differ before allocation",
    "source-role-count": "Textured palette schema or source texture count is inconsistent",
    "collection-role-count": "Textured source aggregate exceeds namespace/collection limits"}


def cold_ids(profile: str) -> tuple[str, ...]:
    return (PROFILE_IDS if profile == "cold-source" else ()) + tuple(
        case + "-" + operation if case == "palette-reread-oversize" else case
        for case in COLD_CASES[profile] for operation in (("verify", "read", "read_path") if case == "palette-reread-oversize" else ("",)))


def no_reparse(path: Path) -> None:
    for current in (path, *path.parents):
        if current.exists() or current.is_symlink():
            require(not current.is_symlink() and not current.is_junction() and
                    not current.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT,
                    f"Unexpected reparse path: {current}")


def inventory(root: Path, aliases: dict[Path, Path] | None = None) -> list[dict]:
    aliases = aliases or {}
    no_reparse(root)
    rows = []
    pending = [root]
    while pending:
        for path in pending.pop().iterdir():
            attributes = path.lstat().st_file_attributes
            if attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                require(path in aliases and path.is_junction() and path.resolve(strict=True) == aliases[path] and
                        aliases[path].is_dir(), f"Unexpected retained alias: {path}")
                no_reparse(aliases[path])
                continue
            if path.is_dir():
                pending.append(path)
            else:
                require(path.is_file(), "Nonregular retained file")
                rows.append({"relative_path": path.relative_to(root).as_posix(), "path": str(path),
                             "bytes": path.stat().st_size, "sha256": digest(path).upper()})
    return sorted(rows, key=lambda row: row["path"])


def total(rows: list[dict]) -> int:
    require(all(type(row["bytes"]) is int and row["bytes"] >= 0 for row in rows), "Invalid byte totals")
    return sum(row["bytes"] for row in rows)


def indexed_inventory(rows: list[dict]) -> dict[str, dict]:
    """Compare PS inventory rows by exact path, independent of culture sorting."""
    require(type(rows) is list and all(type(row) is dict and
            set(row) == {"relative_path", "path", "bytes", "sha256"} and
            type(row["path"]) is str and type(row["relative_path"]) is str for row in rows),
            "PowerShell inventory row shape differs")
    require(len(rows) == len({row["path"] for row in rows}) == len({row["path"].casefold() for row in rows}) and
            len(rows) == len({row["relative_path"] for row in rows}) == len({row["relative_path"].casefold() for row in rows}),
            "PowerShell inventory contains duplicate or aliased paths")
    return {row["path"]: row for row in rows}


def native_inventory_index(rows: list[dict], label: str) -> dict[str, dict]:
    """Native filesystem component order need not equal Python string order."""
    require(type(rows) is list and all(
        type(row) is dict and set(row) == {"path", "bytes", "sha256"} and
        type(row["path"]) is str and bool(row["path"]) and
        "\\" not in row["path"] and not row["path"].startswith("/") and
        all(part not in {"", ".", ".."} for part in row["path"].split("/")) and
        type(row["bytes"]) is int and row["bytes"] >= 0 and
        type(row["sha256"]) is str and len(row["sha256"]) == 64 and
        all(char in "0123456789abcdef" for char in row["sha256"])
        for row in rows), "Native inventory row shape/types differ: " + label)
    require(len(rows) == len({row["path"] for row in rows}) ==
            len({row["path"].casefold() for row in rows}),
            "Native inventory has duplicate/aliased paths: " + label)
    return {row["path"]: row for row in rows}


def verify_storage(build: Path, objects: Path, report: dict) -> None:
    limits = {"maximum_bytes_each": MAXIMUM, "minimum_free_bytes": MINIMUM_FREE, "aggregate_limit_claimed": False}
    require(report["artifact_root_limits"] == limits, "Build separate-root limits differ")
    ledger = load(build / "artifact-storage.json")
    require(set(ledger) == {"schema", "artifact_roots", "maximum_bytes_each", "minimum_free_bytes", "observed_free_bytes",
                           "aggregate_bytes", "aggregate_limit_claimed"} and ledger["schema"] == 1 and
            ledger["maximum_bytes_each"] == MAXIMUM and ledger["minimum_free_bytes"] == MINIMUM_FREE and
            ledger["aggregate_limit_claimed"] is False and type(ledger["observed_free_bytes"]) is int and
            ledger["observed_free_bytes"] >= MINIMUM_FREE, "Storage evidence scope differs")
    expected = [{"root": str(root), "file_count": len(inventory(root)), "total_bytes": total(inventory(root))}
                for root in (build, objects)]
    require(ledger["artifact_roots"] == expected and all(row["total_bytes"] <= MAXIMUM for row in expected) and
            ledger["aggregate_bytes"] == sum(row["total_bytes"] for row in expected), "Actual individual/aggregate storage differs")
    prior = report["artifact_storage_before_report"]
    excluded = {"build-report.json", "protected-after.json", "artifact-storage.json"}
    prior_rows = [row for row in inventory(build) if row["relative_path"] not in excluded]
    require(prior == {"runtime_bytes": total(prior_rows), "object_bytes": expected[1]["total_bytes"],
                      "aggregate_bytes": total(prior_rows) + expected[1]["total_bytes"], "report_and_final_preservation_excluded": True},
            "Pre-report storage witness differs")

def expand_sdk_trees(report: dict) -> dict:
    require(report["external_header_tree_format"] == "root-relative-sha256-base64-triplets-v1",
            "Unknown or missing compact SDK evidence format")
    raw = report["external_header_trees"]
    require(set(raw) == {"trees_before", "trees_after"}, "SDK observation fields differ")
    expanded = {}
    for observation, trees in raw.items():
        require(type(trees) is list and len(trees) == 4, "Four SDK observations required")
        result = []
        for tree in trees:
            require(set(tree) == {"root", "files"} and type(tree["root"]) is str and
                    Path(tree["root"]).is_absolute() and type(tree["files"]) is list,
                    "Compact SDK tree fields differ")
            rows = []
            for record in tree["files"]:
                require(type(record) is list and len(record) == 3 and type(record[0]) is str and
                        type(record[1]) is int and record[1] >= 0 and type(record[2]) is str,
                        "Compact SDK record types differ")
                relative = Path(record[0])
                require(not relative.anchor and relative.as_posix() == record[0] and relative.parts and
                        ":" not in record[0] and ".." not in relative.parts,
                        "Compact SDK relative path escaped its root")
                digest_bytes = base64.b64decode(record[2], validate=True)
                require(len(digest_bytes) == 32 and base64.b64encode(digest_bytes).decode("ascii") == record[2],
                        "Compact SDK SHA256 is not canonical32-byte base64")
                rows.append({"relative_path": record[0], "path": str(Path(tree["root"]) / relative),
                             "bytes": record[1], "sha256": digest_bytes.hex().upper()})
            require(len(rows) == len({row["relative_path"].casefold() for row in rows}), "Duplicate compact SDK path")
            result.append({"root": tree["root"], "files": rows})
        expanded[observation] = result
    return expanded

def verify_build(build: Path, objects: Path, kit: Path, ui_kit: Path, material: Path, neutral: Path, painted: Path, cpu: Path) -> tuple[dict, set[Path]]:
    report = load(build / "build-report.json")
    repository = Path(report["authoritative_repository"])
    require(repository == Path(r"C:\Users\fipst\Desktop\ribterra"), "Authoritative repository differs")
    require(Path(report["output_root"]) == build and Path(report["control_kit"]["root"]) == kit,
            "Build/kit binding mismatch")
    require(report["build_label"] == "gameview-textured-material-isolated-unversioned" and
            report["translation_unit_count"] == 24 and
            report["internal_production_translation_unit_count"] == 23 and
            len(report["translation_units"]) == 24 and len(set(report["translation_units"])) == 24 and
            set(report["translation_units"]) == TRANSLATION_UNITS, "Fresh normal material TU closure differs")
    require(not any(report["execution"].values()) and not any(report["claims"].values()),
            "Builder overstates execution or delivery")
    environment = report["compiler_environment"]
    require(set(environment) == {"include", "lib", "vs_install_dir", "vc_tools_version",
                                 "windows_sdk_dir", "windows_sdk_version"} and
            all(isinstance(value, str) and value.strip() for value in environment.values()),
            "MSVC/Windows SDK compile environment is not fully identified")
    checked: set[Path] = set()
    require(Path(report["original_art"]["root"]) == repository / "assets" and
            Path(report["material_collection"]["root"]) == material and
            len(report["original_art"]["files"]) == 181 and
            len(report["original_art"]["catalog_files"]) == 69 and
            len(report["material_collection"]["files"]) == 23 and
            report["original_art"]["bulk_staged"] is False and
            report["material_collection"]["bulk_staged"] is False and
            report["material_collection"]["production_accepted"] is False,
            "Normal composition original/material closure or scope differs")
    for section, root, count in (("original_art", repository / "assets", 181), ("material_collection", material, 23), ("material_neutral_compatibility", neutral, 20)):
        rows = report[section]["files"]
        require(len(rows) == count and len({r["relative_path"] for r in rows}) == count and
                {r["relative_path"] for r in rows} == {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()} and
                all(Path(r["path"]) == root / r["relative_path"] for r in rows), "Exact referenced asset tree differs")
        for row in rows:
            checked_path = Path(row["path"])
            for ancestor in (checked_path, *checked_path.parents):
                require(not ancestor.is_symlink() and not ancestor.is_junction(), "Referenced asset traverses reparse path")
    compatibility = report["material_painted_compatibility"]
    require(Path(compatibility["root"]) == painted and compatibility["bulk_staged"] is False and compatibility["production_accepted"] is False and
            indexed_inventory(compatibility["files"]) == indexed_inventory(inventory(painted)) and
            len(compatibility["files"]) == 18, "Current painted compatibility closure differs")
    bound_rows(report, checked)
    external = expand_sdk_trees(report)
    expected_external = {
        repository / ".deps/SDL3-3.4.16/include",
        repository / ".deps/SDL3_ttf-3.2.2/include",
        repository / "vcpkg_installed/x64-windows-release/include/GameNetworkingSockets",
        repository / "vcpkg_installed/x64-windows-release/include",
    }
    require(set(external) == {"trees_before", "trees_after"} and
            len(external["trees_before"]) == len(external["trees_after"]) == 4 and
            {Path(tree["root"]) for tree in external["trees_before"]} == expected_external and
            external["trees_before"] == external["trees_after"], "Accepted external header closure differs")
    for tree in external["trees_before"]:
        tree_root = Path(tree["root"])
        rows = tree["files"]
        actual = {p.relative_to(tree_root).as_posix() for p in tree_root.rglob("*") if p.is_file()}
        require(rows and len(rows) == len({r["relative_path"] for r in rows}) == len(actual) and
                {r["relative_path"] for r in rows} == actual and
                all(Path(r["path"]) == tree_root / r["relative_path"] for r in rows), "External header file set differs")
        for path in (tree_root, *tree_root.parents, *tree_root.rglob("*")):
            require(not path.is_symlink() and not path.is_junction(), "External SDK traverses reparse path")
    bound_rows(external, checked)
    for row in report["runtime_files"]:
        for field in ("origin", "destination"):
            hash_row(row, Path(row[field]))
            checked.add(Path(row[field]))
    sources = report["source_provenance"]
    source_names = {row["relative_path"] for row in sources}
    required_sources = {
        "tools/gameview_textured_material_probe.cpp", "tools/engine_control_probe.hpp",
        "licenses/Cormorant-OFL.txt", "licenses/Noto-OFL.txt", "licenses/PTSerif-OFL.txt", "licenses/Ribterra-Assets.txt",
        "tools/export_engine_ui_kit.py", "tools/asset_tool.py", "src/ui/game_ui.cpp",
        "include/ribterra/render_frame.hpp", "src/render/renderer2d.cpp",
        *{f"src/ui/game_ui_{name}.inc" for name in ("lobby", "manual", "settlement", "economy", "buildings", "professions")},
        "tools/build_gameview_textured_material_isolated.ps1", "tools/run_gameview_textured_material_isolated.ps1",
        "tools/export_engine_control_kit.py", "tools/export_material_master.py",
        "tests/gameview_textured_material_evidence_tests.py", "tests/gameview_cumulative_evidence_tests.py",
        "tests/gameview_neutral_material_export_tests.py",
        "tests/gameview_ui_evidence_tests.py",
        "tests/gameview_material_semantics_tests.py",
        "tests/gameview_ui_scene_evidence_tests.py", "include/ribterra/game_art.hpp", "src/client/game_art.cpp",
        "tests/engine_control_kit_tests.py", "include/ribterra/weather_presentation.hpp",
        "include/ribterra/view.hpp", "src/client/view.cpp", "src/sim/weather.cpp",
        "src/sim/world.cpp", "src/sim/world_save.cpp", "src/client/client_session.cpp",
        "src/net/protocol.cpp", "shaders/water_ground_lighting.frag.hlsl",
        "shaders/atmosphere_material_forward.frag.hlsl",
    }
    require(len(source_names) == len(sources) and required_sources <= source_names,
            "Normal material source closure missing or duplicate")
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
    protected = report["protected_binding"]
    require(protected and all(row["unchanged"] is True and row["bytes"] == row["after_bytes"] and
            row["sha256"] == row["after_sha256"] for row in protected),
            "Protected prior evidence changed")
    require(load(build / "protected-after.json") == [
        {"path": row["path"], "before_bytes": row["bytes"], "after_bytes": row["after_bytes"],
         "before_sha256": row["sha256"], "after_sha256": row["after_sha256"], "unchanged": True}
        for row in protected], "Final protected input ledger differs")
    sidecars = set(build.glob("*.log.exit.txt"))
    logs = {Path(str(path).removesuffix(".exit.txt")) for path in sidecars}
    require(len(sidecars) == 122 and
            all(path.read_text(encoding="utf-8-sig").strip() == "0" for path in sidecars) and
            {Path(row["path"]) for row in report["command_evidence"]} == sidecars | logs,
            "Actual command sidecars/log inventory differs")
    commands = report["commands"]
    require(len(commands) == 122 and len({row["log"]["path"] for row in commands}) == 122 and
            {Path(row["log"]["path"]) for row in commands} == logs and
            {Path(row["exit_sidecar"]["path"]) for row in commands} == sidecars and
            all(row["exit_code"] == 0 and row["arguments"] and
                Path(row["working_directory"]) == Path(report["authoritative_repository"])
                for row in commands), "Actual argv/cwd/exit ledger differs")
    tool_paths = {Path(row["path"]) for row in report["tools"]}
    require(all(Path(row["executable"]) in tool_paths for row in commands), "Command tool is unbound")
    flags = report["flags"]
    expected_flags = ["/nologo", "/TP", "/DWIN32", "/D_WINDOWS", "/EHsc", "/O2", "/Ob2", "/DNDEBUG",
                      "/std:c++20", "/MT", "/utf-8", "/W4", "/WX", "/permissive-", "/GR",
                      "/Zc:__cplusplus", "/DRMLUI_STATIC_LIB",
                      '/DRIBTERRA_PROBE_BUILD="gameview-textured-material-isolated-unversioned"',
                      "/I" + str(build / "source/include"), "/I" + str(build / "generated")]
    expected_flags += ["/external:I" + str(path) for path in (
        repository / ".deps/SDL3-3.4.16/include", repository / ".deps/SDL3_ttf-3.2.2/include",
        repository / "vcpkg_installed/x64-windows-release/include/GameNetworkingSockets",
        repository / "vcpkg_installed/x64-windows-release/include")]
    expected_flags += ["/external:W0"]
    require(flags == expected_flags and STRICT_FLAGS <= set(flags),
            "Exact strict compiler/include/SDK argument closure differs")
    compiles = [row for row in commands if Path(row["executable"]).name.lower() == "cl.exe"]
    require(len(compiles) == 24, "Compiler invocation count differs")
    object_paths = {Path(row["path"]) for row in report["objects"]}
    expected_objects = {objects / (Path(name).stem + ".obj") for name in TRANSLATION_UNITS}
    require(len(report["objects"]) == 24 and object_paths == expected_objects, "Bound object inventory differs")
    compiled_sources = set()
    for row in compiles:
        argv = row["arguments"]
        require(STRICT_FLAGS <= set(argv) and "/WX-" not in argv and argv.count("/c") == 1,
                "Actual compile lacks strict flags")
        source = Path(argv[argv.index("/c") + 1])
        unit_flags = (["/Zc:inline"] if source.name == "gameview_textured_material_probe.cpp" else
                      ["/DSTEAMNETWORKINGSOCKETS_STATIC_LINK"] if source.name == "gns_transport.cpp" else [])
        require(argv == expected_flags + unit_flags + ["/c", str(source),
                "/Fo" + str(objects / (source.stem + ".obj"))],
                "Actual compile arguments contain unbound inputs or overriding options")
        require(argv.count("/Zc:inline") == (1 if source.name == "gameview_textured_material_probe.cpp" else 0) and
                "/Zc:inline-" not in argv and "/GL" not in argv,
                "Actual probe-only object footprint flags differ")
        compiled_sources.add(source)
        fo = [arg for arg in argv if arg.startswith("/Fo")]
        require(fo == ["/Fo" + str(objects / (source.stem + ".obj"))], "Actual compile object output differs")
    require(compiled_sources == {build / "source" / name for name in report["translation_units"]},
            "Actual compiled sources differ from the bound TU list")
    links = [row for row in commands if Path(row["executable"]).name.lower() == "link.exe"]
    require(len(links) == 1 and links[0]["arguments"] ==
        ["/nologo", "/machine:x64", "/incremental:no", "/debug:none", "/subsystem:console",
         "/out:" + str(build / "GameViewTexturedMaterialProbe.exe")] + [row["path"] for row in report["objects"]] +
        [row["path"] for row in report["link_libraries"]], "Actual link objects/libraries/output differ from bindings")
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
    python = Path(r"C:\Python314\python.exe")
    shadercross = repository / "vcpkg_installed/x64-windows/tools/sdl3-shadercross/shadercross.exe"
    dxc = repository / "vcpkg_installed/x64-windows/tools/directx-dxc/dxc.exe"
    flatc = repository / "vcpkg_installed/x64-windows/tools/flatbuffers/flatc.exe"
    source_shaders, output_shaders = build / "source/shaders", build / "assets/shaders"
    expected_other = {}
    for program in shader["programs"]:
        for stage, stage_name in (("vert", "vertex"), ("frag", "fragment")):
            for extension, destination in (("dxil", "DXIL"), ("json", "JSON")):
                expected_other[f"shader-{program}-{stage}-{extension}.log"] = (shadercross, [
                    str(source_shaders / f"{program}.{stage}.hlsl"), "-s", "HLSL", "-d", destination,
                    "-t", stage_name, "-e", "main", "-I", str(source_shaders), "-o",
                    str(output_shaders / f"{program}.{stage}.{extension}")])
    expected_other["shader-contract.log"] = (python, [str(build / "source/tests/shader_contract.py"),
        str(source_shaders), str(output_shaders), "--dxc", str(dxc), "--shadercross", str(shadercross), "--write-manifest"])
    expected_other["generate-skill-balance.log"] = (python, [str(build / "source/tools/compile_skill_balance.py"),
        str(build / "source/data/skills.json"), str(build / "generated/skill_balance_generated.h")])
    for schema in ("world_save", "network", "network_frame"):
        expected_other[f"generate-{schema}.log"] = (flatc, ["--cpp", "--scoped-enums", "--gen-object-api",
            "-o", str(build / "generated"), str(build / f"source/schemas/{schema}.fbs")])
    other = [row for row in commands if row not in compiles and row not in links]
    require(len(other) == len(expected_other) == 97 and
            {Path(row["log"]["path"]).name.lower() for row in other} == set(expected_other),
            "Exact shader/generator command inventory differs")
    for row in other:
        tool, argv = expected_other[Path(row["log"]["path"]).name.lower()]
        require(Path(row["executable"]) == tool and row["arguments"] == argv,
                "Actual shader/generator command uses unbound inputs or output")
    generated = report["generated_headers"]
    require(len(generated) == 4 and {Path(row["path"]) for row in generated} == {
        build / "generated" / name for name in ("skill_balance_generated.h", "world_save_generated.h",
                                                "network_generated.h", "network_frame_generated.h")},
            "Exact generated header closure differs")
    exe = build / "GameViewTexturedMaterialProbe.exe"
    require(Path(report["executable"]["path"]) == exe, "Executable binding differs")
    require(sum(p.stat().st_size for p in build.rglob("*") if p.is_file()) <= 128 * 1024**2,
            "Build output budget exceeded")
    require(Path(report["ui_kit"]["root"]) == ui_kit and not report["ui_kit"]["bulk_staged"], "UI kit root differs")
    verify_ui_kit(repository, ui_kit, checked)
    for section, root in (("control_kit", kit), ("ui_kit", ui_kit)):
        rows = report[section]["files"]
        require(indexed_inventory(rows) == indexed_inventory(inventory(root)), "Referenced kit binding differs")
    dependencies = report["ui_dependencies"]
    require(dependencies["rmlui_version"] == "6.3" and dependencies["static_core"] is True and
            dependencies["bulk_copied"] is False, "Installed UI SDK policy differs")
    dependency_before = {row["path"]: row for row in dependencies["files_before"]}
    dependency_after = {row["path"]: row for row in dependencies["files_after"]}
    require(len(dependency_before) == len(dependencies["files_before"]) and dependency_before.keys() == dependency_after.keys(),
            "UI SDK metadata/header binding differs")
    expected_dependencies = {str(p) for p in (repository / "vcpkg_installed/x64-windows-release/include/RmlUi").rglob("*") if p.is_file()}
    for folder in ("rmlui", "freetype"):
        expected_dependencies |= {str(p) for p in (repository / "vcpkg_installed/x64-windows-release/share" / folder).glob("*.cmake")}
    require(dependency_before.keys() == expected_dependencies, "UI SDK current inventory differs")
    final_libs = {row["path"]: row for row in report["link_libraries_final_binding"]}
    require(len(final_libs) == len(report["link_libraries"]) and final_libs.keys() == {row["path"] for row in report["link_libraries"]},
            "Link library final inventory differs")
    for row in report["link_libraries"]:
        after = final_libs[row["path"]]
        require(after["unchanged"] is True and row["bytes"] == after["bytes"] == after["after_bytes"] and
                row["sha256"] == after["sha256"] == after["after_sha256"], "Link library changed during build")
    require({Path(row["path"]).name for row in report["link_libraries"]} >=
            {"rmlui.lib", "freetype.lib", "bz2.lib", "brotlidec.lib", "brotlicommon.lib"}, "Static UI libraries missing")
    require("/DRMLUI_STATIC_LIB" in flags and all("/DRMLUI_STATIC_LIB" in row["arguments"] for row in compiles),
            "Static RmlUi declaration lacks actual compile binding")
    for path, after_row in dependency_after.items():
        before_row = dependency_before[path]
        require(after_row["unchanged"] is True and before_row["sha256"] == after_row["sha256"] == after_row["after_sha256"] and
                before_row["bytes"] == after_row["bytes"] == after_row["after_bytes"], "UI SDK changed during build")
    require(Path(report["object_root"]) == objects and report["object_artifact"]["root"] == str(objects), "Object root linkage differs")
    require(indexed_inventory(report["object_artifact"]["files"]) == indexed_inventory(inventory(objects)) and
            report["object_artifact"]["file_count"] == 24 and
            report["object_artifact"]["total_bytes"] == total(inventory(objects)), "Object storage inventory differs")
    require(report["objects_before_link"] == report["objects"], "Objects changed during actual link")
    verify_storage(build, objects, report)
    for input_root, section, count in ((material, "material_source_inputs", 35), (neutral, "neutral_source_inputs", 20), (painted, "painted_source_inputs", 14)):
        verify_producer_inputs(input_root, section, count, repository, report, checked)
    verify_prerequisites(report, cpu, checked)
    return report, checked


def verify_prerequisites(report: dict, cpu_path: Path, checked: set[Path]) -> None:
    """Rehash accepted raw closures with the exact adoption and compile-repair hops.

    This does not replay an old checker against changed current C++ sources.
    Existing actual receipts remain evidence of their independently executed
    algorithm/renderer tests; every referenced retained byte is observed again.
    """
    state = REPOSITORY / ".unlazy/engine-completion-2026-10-08"
    pins = {
        "textured-material-cpu-accepted-2026-10-09.json": "F6CEE16A21AA2F6A0087E65B8EF342CA170DBF5E4C3A18C95270865E708616CB",
        "house-texture-source-CPU-accepted-2026-10-09.json": "AE44394E7C75D2B38730A5204D4921E93498E35FAF6B207F746585529D1DBBB6",
        "neutral-main-accepted-2026-10-10.json": "F2A13C5A5B853E6FD0219F5924EDFA9D92F3286DA169379CAF9D66DB1BFEA5A1",
        "textured-runtime-adoption-2026-10-10/receipt.json": "27FBAC0AB23605344D89346A7CF032DA6984DEBE39FCB5A240D64342459139D1",
        "textured-runtime-compile-repair-2026-10-10/receipt.json": "2788220B642C03F631C4A1152FD77CECECEA3D28B1D1CC36E4B135ADBCE3A32D"}
    observed: dict[Path, dict] = {}
    def current(path: Path) -> dict:
        no_reparse(path)
        require(path.is_file(), "Prerequisite is not a regular file")
        row = {"path": str(path), "bytes": path.stat().st_size, "sha256": digest(path).upper()}
        require(path not in observed or observed[path] == row, "Prerequisite changed during observation")
        observed[path] = row
        checked.add(path)
        return row
    documents = []
    for name, expected_sha in pins.items():
        path = state / name
        require(current(path)["sha256"] == expected_sha, "Accepted prerequisite receipt changed")
        documents.append(load(path))
    cpu_accept, house_accept, main_accept, adoption, repair = documents
    repair_relative = "src/assets/asset_catalog.cpp"
    repair_live = REPOSITORY / repair_relative
    intermediate = (45490, "23BFF6A13546E8DCB4C4DDFCE6044B96B406EFC9D6536211E55B6E241DD36B6A")
    repaired = (45917, "35D3705EBCB9A8C6E820D8A2B27F0FC41520BE379FFC88EDC7A16F9EFF059B3C")
    require(set(repair) == {"schema", "kind", "applied_at_utc", "branch", "head", "accepted_source_only",
            "independent_source_review", "failed_build", "applied", "unchanged_runtime", "original_adoption",
            "compile_accepted", "native_accepted", "automatic_gate_certification"} and
            repair["schema"] == 1 and repair["kind"] == "ribterra-textured-runtime-compile-repair-v1" and
            repair["branch"] == "AInora/first-playable" and repair["head"] == "bdc53e82c3d65eee4a5d8b13497aea67dff4a33b" and
            repair["accepted_source_only"] is True and repair["compile_accepted"] is False and
            repair["native_accepted"] is False and repair["automatic_gate_certification"] is False and
            len(repair["applied"]) == 1, "Compile-repair receipt scope differs")
    repair_entry = repair["applied"][0]
    require(set(repair_entry) == {"relative_path", "historical_origin", "before", "failed_source_snapshot", "candidate", "after"} and
            repair_entry["relative_path"] == repair_relative and Path(repair_entry["historical_origin"]) == repair_live and
            Path(repair_entry["before"]["path"]) == state / "textured-runtime-compile-repair-2026-10-10/source-preimages" / repair_relative and
            Path(repair_entry["failed_source_snapshot"]["path"]) == REPOSITORY /
                "build/gameview-textured-material-2026-10-10-attempt01/source" / repair_relative and
            Path(repair_entry["candidate"]["path"]) == state / "textured-json-compile-repair-candidate" / repair_relative and
            Path(repair_entry["after"]["path"]) == repair_live,
            "Compile-repair archive/failed snapshot/candidate/current linkage differs")
    def repair_identity(field: str, identity: tuple[int, str]) -> dict:
        record = repair_entry[field]
        require(set(record) == {"path", "bytes", "sha256"} and
                (record["bytes"], record["sha256"].upper()) == identity, "Compile-repair pinned source identity differs")
        row = current(Path(record["path"]))
        require((row["bytes"], row["sha256"]) == identity, "Compile-repair retained/current source changed")
        return row
    repair_before = repair_identity("before", intermediate)
    repair_failed = repair_identity("failed_source_snapshot", intermediate)
    repair_candidate = repair_identity("candidate", repaired)
    repair_after = repair_identity("after", repaired)
    repair_mappings = [{"relative_path": repair_relative, "historical_origin": str(repair_live), "before": repair_before,
        "failed_source_snapshot": repair_failed, "candidate": repair_candidate, "current_after": repair_after}]
    original_adoption = repair["original_adoption"]
    require(set(original_adoption) == {"path", "bytes", "sha256"} and
            Path(original_adoption["path"]) == state / "textured-runtime-adoption-2026-10-10/receipt.json" and
            original_adoption["bytes"] == 3218 and original_adoption["sha256"] == pins["textured-runtime-adoption-2026-10-10/receipt.json"],
            "Compile repair no longer binds the original adoption receipt")
    failed_record = repair["failed_build"]
    require(set(failed_record) == {"path", "bytes", "sha256"} and
            Path(failed_record["path"]) == state / "textured-build-failure-2026-10-10-attempt01.json" and
            failed_record["bytes"] == 35627 and failed_record["sha256"] == "7DD2758FF49B90E4B3250543240958FEEBDEB92FAF850EAAE78603632F6CE1C1",
            "Compile repair failed-build binding differs")
    failed_row = current(Path(failed_record["path"]))
    require(failed_row == failed_record, "Retained failed-build receipt changed")
    failed_build = load(Path(failed_record["path"]))
    require(failed_build["schema"] == 1 and failed_build["kind"] == "ribterra-textured-gameview-strict-build-failure-v1" and
            failed_build["actual_parent_exit"] == 1 and failed_build["actual_native_exit"] == 2 and
            failed_build["failed_native_stage"] == "compile-asset_catalog" and failed_build["protected_unchanged"] is True and
            failed_build["accepted_build"] is False and failed_build["native_executed"] is False and failed_build["preserved"] is True,
            "Preserved first build is not the explicit failed strict compile")
    # The immutable failure JSON retains historical helper origins. Its own hash
    # and direct repaired-source snapshot are checked; no helper epoch is waived.
    require(cpu_accept["accepted_CPU_only"] is True and cpu_accept["parent_exit"] == cpu_accept["independent_exit"] == 0 and
            cpu_accept["actual_commands"] == 92 and cpu_accept["successful_commands"] == 29 and cpu_accept["expected_rejects"] == 63 and
            house_accept["accepted_source_CPU_only"] is True and house_accept["independent_actual_review"]["exit"] == 0 and
            main_accept["accepted"] is True and main_accept["actual_processes"] == 10 and main_accept["captures"] == 12 and
            main_accept["independent_guards"] == 3890, "Actual prerequisite acceptance scope differs")
    expected_names = {"include/ribterra/asset_catalog.hpp", "src/assets/asset_catalog.cpp", "src/client/view.cpp", "src/client/main.cpp"}
    require({entry["relative_path"] for entry in adoption["applied"]} == expected_names and len(adoption["applied"]) == 4,
            "Historical source epoch is not exactly four files")
    mappings, old = [], {}
    for entry in adoption["applied"]:
        relative = entry["relative_path"]
        live = REPOSITORY / relative
        archive = Path(entry["before"]["path"])
        snapshot_root = "gameview-neutral-material-main-2026-10-09-attempt02" if relative == "src/client/main.cpp" else "gameview-neutral-material-2026-10-09-attempt06"
        snapshot = REPOSITORY / "build" / snapshot_root / "source" / relative
        require(archive == state / "textured-runtime-adoption-2026-10-10/source-preimages" / relative and Path(entry["after"]["path"]) == live,
                "Historical archive/live linkage differs")
        a, b, c = current(archive), current(snapshot), current(live)
        require(a["bytes"] == b["bytes"] == entry["before"]["bytes"] and
                a["sha256"] == b["sha256"] == entry["before"]["sha256"].upper(), "Historical compiled source identity differs")
        old[live] = {(a["bytes"], a["sha256"])}
        if relative == repair_relative:
            require((a["bytes"], a["sha256"]) == (11207, "364983BF47A07FDB5891C47AC1CEEA6FED7A8EFD86693FB1758802B30489C707") and
                    (entry["after"]["bytes"], entry["after"]["sha256"].upper()) == intermediate and
                    (c["bytes"], c["sha256"]) == repaired, "AssetCatalog adoption/compile-repair chain differs")
            old[live].add(intermediate)
        else:
            require(c["bytes"] == entry["after"]["bytes"] and c["sha256"] == entry["after"]["sha256"].upper(),
                    "Unchanged adopted runtime source differs")
        mappings.append({"relative_path": relative, "historical_origin": str(live), "expected_old_bytes": a["bytes"],
            "expected_old_sha256": a["sha256"], "archived_preimage": a, "compiled_snapshot": b, "current_after": c})
    unchanged = repair["unchanged_runtime"]
    require(len(unchanged) == 3 and {Path(row["path"]) for row in unchanged} ==
            {REPOSITORY / relative for relative in expected_names - {repair_relative}} and
            all(set(row) == {"path", "bytes", "sha256"} and row == current(Path(row["path"])) for row in unchanged),
            "Compile repair changed a different runtime source")
    def observe(value: object) -> None:
        if type(value) is dict:
            if {"path", "bytes", "sha256"} <= value.keys() and type(value["path"]) is str and Path(value["path"]).is_absolute():
                path = Path(value["path"])
                require(type(value["bytes"]) is int and value["bytes"] >= 0 and type(value["sha256"]) is str and len(value["sha256"]) == 64,
                        "Malformed prerequisite identity")
                identity = (value["bytes"], value["sha256"].upper())
                if path not in old or identity not in old[path]:
                    row = current(path)
                    require((row["bytes"], row["sha256"]) == identity, "Accepted raw prerequisite changed: " + str(path))
            for child in value.values():
                observe(child)
        elif type(value) is list:
            for child in value:
                observe(child)
    for document in documents:
        observe(document)
    selected = [cpu_accept["parent_report"], cpu_accept["independent_receipt"], house_accept["parent_review"],
                house_accept["independent_actual_review"], main_accept["parent"], main_accept["independent"]]
    loaded = []
    for record in selected:
        observe(record)
        loaded.append(load(Path(record["path"])))
    for document in loaded:
        observe(document)
    cpu, independent_cpu, _, independent_house, _, independent_main = loaded
    require(cpu_path == Path(cpu_accept["parent_report"]["path"]) and cpu_path.stat().st_size <= 4 * 1024**2 and
            cpu["complete"] is True and cpu["kind"] == "ribterra-textured-material-independent-cpu-v1" and
            cpu["source_only"] is False and cpu["errors"] == [] and len(cpu["commands"]) == 92 and
            cpu["measured"]["full_lifetime_counter_complete"] is True and len(cpu["measured"]["child_processes"]) == 92 and
            independent_cpu["accepted"] is True and independent_house["accepted"] is True and
            independent_main["complete"] is True and independent_main["exit_code"] == 0,
            "CPU/House/main raw proof is incomplete or uses the wrong evidence role")
    for field, expected_sha in (("producer", "820F5AACD2BBF8CC5CC95984250DCF95AD8357EB3EF9D8AFE42A981A83752699"),
            ("test_source", "7DDDB89676DA31B7CDA25697A6F62E6304F751CBB0CB1FA2E01698789907DDB5")):
        require(cpu[field]["sha256"].upper() == expected_sha and current(Path(cpu[field]["path"]))["sha256"] == expected_sha,
                "Independent producer/oracle pin differs")
    accepted_house_exports = {Path(row["root"]) for row in independent_house["retained_roots"] if row["files"] == 23}
    accepted_neutral_exports = {Path(row["root"]) for row in independent_house["retained_roots"] if row["files"] == 20}
    require(len(accepted_house_exports) == 2 and len(accepted_neutral_exports) == 1 and
            Path(report["material_collection"]["root"]) in accepted_house_exports and
            Path(report["material_neutral_compatibility"]["root"]) in accepted_neutral_exports,
            "Selected textured/neutral collection is not an independently accepted House raster/counterpart")
    aliases = {}
    for alias in independent_cpu["owned_aliases"]:
        require(alias["followed_in_inventory"] is False, "CPU alias was followed")
        aliases[Path(alias["path"])] = Path(alias["target"])
    trees = []
    for record in independent_cpu["retained_roots"] + independent_house["retained_roots"]:
        root = Path(record["root"])
        require(root.is_relative_to(REPOSITORY) and root.is_dir(), "Prerequisite root escaped repository")
        rows = sorted(inventory(root, aliases), key=lambda row: row["relative_path"])
        canonical_rows = [{"relative_path": row["relative_path"], "bytes": row["bytes"], "sha256": row["sha256"].lower()} for row in rows]
        inventory_sha = hashlib.sha256((json.dumps(canonical_rows, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False) + "\n").encode()).hexdigest().upper()
        require(len(rows) == record["files"] and total(rows) == record["bytes"] <= MAXIMUM and inventory_sha == record["inventory_sha256"].upper(),
                "Accepted retained-root membership/bytes/hash changed")
        for row in rows:
            current(Path(row["path"]))
        trees.append({"root": str(root), "files": rows, "file_count": len(rows), "total_bytes": total(rows), "accepted_inventory_sha256": inventory_sha})
    require(len(independent_main["guard_before"]) == len(independent_main["guard_after"]) == 3890 and type(independent_main["changed_guards"]) is list and independent_main["changed_guards"] == [] and
            independent_main["guard_before"] == independent_main["guard_after"], "Historical main preservation differs")
    memberships = independent_main["evidence_membership"]
    require(memberships["exact"] is True and memberships["before"] == memberships["after"] and memberships["root_count"] == 8,
            "Historical main root membership was not preserved")
    for root_name, rows in memberships["before"].items():
        root = Path(root_name)
        alias_map = {root / row["relative_path"]: Path(row["target"]) for row in rows if row["kind"] == "owned-junction"}
        require(all(row.get("followed") is False for row in rows if row["kind"] == "owned-junction"), "Historical main followed an alias")
        files = inventory(root, alias_map)
        expected_files = {row["relative_path"] for row in rows if row["kind"] == "file"}
        require(len(rows) == len(expected_files) + len(alias_map) and {row["relative_path"] for row in files} == expected_files,
                "Historical main physical membership changed")
    expected = report["accepted_prerequisites"]
    require(set(expected) == {"classification", "cpu_proof", "files", "historical_mappings", "compile_repair_mappings", "retained_trees", "retained_aliases",
            "main_evidence_membership", "CPU_role_maximum_bytes", "old_current_source_verifiers_invoked"}, "Prerequisite field set differs")
    main_rows = expected["main_evidence_membership"]
    require(len(main_rows) == len({row["root"].casefold() for row in main_rows}) == 8 and
            {row["root"] for row in main_rows} == set(memberships["before"]), "Declared main membership root set differs")
    for declaration in main_rows:
        members = declaration["members"]
        require(set(declaration) == {"root", "members"} and len(members) == len({row["relative_path"].casefold() for row in members}) and
                {row["relative_path"]: row for row in members} == {row["relative_path"]: row for row in memberships["before"][declaration["root"]]},
                "Builder historical main membership differs from independently accepted source")
    require(expected == report["accepted_prerequisites_final"] and expected["classification"] ==
            "accepted historical CPU/House/main bytes plus exact4source epoch mappings and AssetCatalog compiler repair; no oracle or native replay" and
            expected["CPU_role_maximum_bytes"] == 4194304 and expected["old_current_source_verifiers_invoked"] is False and
            expected["cpu_proof"] == observed[cpu_path] and expected["historical_mappings"] == mappings and
            expected["compile_repair_mappings"] == repair_mappings, "Prerequisite declaration shape differs")
    require(all(set(row) == {"path", "bytes", "sha256"} for row in expected["files"]) and
            len({row["path"].casefold() for row in expected["files"]}) == len(expected["files"]) and
            {Path(row["path"]): row for row in expected["files"]} == observed and len(expected["files"]) == len(observed),
            "Builder omitted or added an accepted prerequisite binding")
    require(len(expected["retained_trees"]) == len(trees) and expected["retained_aliases"] == independent_cpu["owned_aliases"], "Prerequisite retained closure differs")
    for declared, actual in zip(expected["retained_trees"], trees, strict=True):
        require({key: value for key, value in declared.items() if key != "files"} == {key: value for key, value in actual.items() if key != "files"} and
                indexed_inventory(declared["files"]) == indexed_inventory(actual["files"]), "Prerequisite tree differs")


def verify_producer_inputs(input_root: Path, section: str, count: int, repository: Path, report: dict, checked: set[Path]) -> None:
    producer = load(input_root / "material-collection-report.json")
    require(producer["inputs_before"] == producer["inputs_after"] and len(producer["inputs_before"]) == count,
            "Producer source observations differ")
    expanded_sources = []
    for row in producer["inputs_before"]:
        relative = Path(row["relative_path"])
        require(set(row) == {"relative_path", "bytes", "sha256"} and not relative.anchor and relative.parts and
                relative.as_posix() == row["relative_path"] and ":" not in row["relative_path"] and
                all(part not in {".", ".."} for part in relative.parts), "Unsafe producer source path")
        path = repository / relative
        no_reparse(path)
        hash_row(row, path)
        checked.add(path)
        expanded_sources.append({"relative_path": row["relative_path"], "path": str(path), "bytes": row["bytes"], "sha256": row["sha256"].upper()})
    require(report[section] == {"files_before": expanded_sources, "files_after": expanded_sources},
            "Builder omitted or changed by-reference material source closure")


def lower_rows(rows: list[dict]) -> list[dict]:
    return [{"path": row["relative_path"], "bytes": row["bytes"], "sha256": row["sha256"].lower()} for row in rows]


def source_record(item: dict) -> dict:
    keys = ["master_sha256", "registration", "geometry_provenance"]
    if "albedo_source" in item:
        keys += ["albedo_source", "generated_albedo_sha256"]
    return {key: item[key] for key in keys}


def source_hash(value: object) -> str:
    return hashlib.sha256(canonical(value) + b"\n").hexdigest()


def pool(rgba: bytes, width: int, height: int) -> tuple[list[int], bytes, bytes]:
    scale = min(1., f32(192. / max(width, height)))
    mw, mh = max(1, math.floor(f32(width * scale) + .5)), max(1, math.floor(f32(height * scale) + .5))
    alpha = rgba[3::4]
    values = bytes(max(alpha[y * width + x]
                       for y in range(cy * height // mh, max(cy * height // mh + 1, (cy + 1) * height // mh))
                       for x in range(cx * width // mw, max(cx * width // mw + 1, (cx + 1) * width // mw)))
                   for cy in range(mh) for cx in range(mw))
    return [mw, mh], values, bytes(255 if value >= 64 else 0 for value in values)


def collection_sources(material: Path, data: dict | None, neutral_mode: bool = True) -> dict[str, dict]:
    original = REPOSITORY / "assets"
    world, art = load(original / "art/world.json"), load(material / "art/world.json")
    metadata, manifest = load(material / "art/material-collection.json"), load(material / "manifest.json")
    manifest_content(manifest)
    require(metadata["schema"] == 1 and metadata["kind"] == "ribterra-material-collection-v1" and
            metadata["item_count"] == len(metadata["items"]) == len(art["materials"]) == len(art["lit_sprites"]) == 2 and
            len(art["textures"]) == 12 and art["schema"] == 2 and art["pages"] == [], "Registered collection shape differs")
    require(set(item["legacy_key"] for item in metadata["items"]) == set(KEYS), "Mapped keys differ")
    textures = {row["id"]: row for row in art["textures"]}
    materials = {row["id"]: row for row in art["materials"]}
    sprites = {row["id"]: row for row in art["lit_sprites"]}
    registrations = data["registrations"] if data is not None else [None] * len(metadata["items"])
    if data is not None:
        require([row["legacy_key"] for row in registrations] == [item["legacy_key"] for item in metadata["items"]], "Registration order/identity differs")
    result = {}
    for item, actual in zip(metadata["items"], registrations, strict=True):
        key, registration = item["legacy_key"], item["registration"]
        page, entry, page_size, paint = original_sprite(original, world, key)
        record, catalog_hash = source_record(item), source_hash({"page": {"path": page["path"]}, "sprite": entry})
        expected = {"legacy_key": key, "sprite_id": item["sprite_id"], "material_id": item["material_id"],
                    "source_hash": source_hash(record), "source_record_sha256": source_hash(record), "source_record": record,
                    "catalog_entry_sha256": catalog_hash}
        if not neutral_mode:
            expected = {name: expected[name] for name in ("legacy_key", "sprite_id", "material_id", "source_record", "source_record_sha256")}
        require((actual is None or actual == expected) and item["source_hash"] == source_hash(record) == materials[item["material_id"]]["source_hash"], "Flattened source identity differs")
        require(registration["page_path"] == page["path"] and registration["rect"] == entry["rect"] and
                registration["pivot"] == entry["pivot"] and registration["source_size"] == entry["rect"][2:] and
                registration["reference_width"] == f32(entry.get("reference_width", entry["rect"][2])) and
                registration["png_sha256"] == digest(original / page["path"]) and registration["decoded_crop_sha256"] == hashlib.sha256(paint).hexdigest() and
                registration["catalog_entry_sha256"] == catalog_hash, "Original registration evidence differs")
        close_values(registration["pivot_px"], [f32(f32(value) * extent) for value, extent in zip(entry["pivot"], entry["rect"][2:])], .000001)
        channels = {}
        formats = ("rgba8_unorm_srgb", "rgba8_unorm", "r16_unorm", "r8_unorm", "rgba8_unorm", "r8_unorm")
        for name, fmt, stride in zip(("albedo", "normal_material", "camera_depth", "emission_mask", "surface_mask", "caster_mask"), formats, (4,4,2,1,4,1), strict=True):
            slot = materials[item["material_id"]]["channels"][name]
            texture = textures[slot["texture"]]
            width, height = registration["source_size"]
            raw = (material / texture["path"]).read_bytes()
            require(slot["rect"] == [0,0,width,height] and [texture["width"],texture["height"]] == [width,height] and
                    texture["format"] == fmt and texture["layout"] == "tight_le" and texture["mip_levels"] == 1 and
                    texture["bytes"] == len(raw) == width*height*stride and texture["sha256"] == hashlib.sha256(raw).hexdigest(), "Delivered six-channel layout/hash differs")
            channels[name] = raw
        authored = "albedo_source" in item
        require(authored is (neutral_mode and key == KEYS[0]), "Explicit/absent source mode differs")
        if authored:
            source = item["albedo_source"]
            require(source["kind"] in {"authored-xyz-triangle-materials-v1", "authored-xyz-triangle-textures-v1"} and
                    digest(material / source["palette_path"]) == source["palette_sha256"] and
                    digest(material / source["provenance"]["notice_path"]) == source["provenance"]["notice_sha256"] and
                    item["generated_albedo_sha256"] == hashlib.sha256(channels["albedo"]).hexdigest(), "Packed authored palette/notice/generated identity differs")
            old, new = paint[3::4], channels["albedo"][3::4]
            counts = {"original_alpha_samples":sum(a != 0 for a in old), "generated_alpha_samples":sum(a != 0 for a in new),
                      "overlap_alpha_samples":sum(a != 0 and b != 0 for a,b in zip(old,new)),
                      "omitted_original_alpha_samples":sum(a != 0 and b == 0 for a,b in zip(old,new)),
                      "generated_only_alpha_samples":sum(a == 0 and b != 0 for a,b in zip(old,new))}
            require(item["coverage"] == counts and set(new) == {0,255} and counts["omitted_original_alpha_samples"] == 19649 and
                    item["supported_alpha_samples"] == counts["generated_alpha_samples"] and item["uncovered_alpha_samples"] == counts["omitted_original_alpha_samples"], "Authored coverage differs or hides the original gap")
        else:
            require(channels["albedo"] == paint, "Absent-mode painted RGBA changed")
        result[key] = {"page":page,"entry":entry,"page_size":page_size,"paint":paint,"rgba":channels["albedo"],"channels":channels,
                       "pool":pool(channels["albedo"],*registration["source_size"]),"original_pool":pool(paint,*registration["source_size"]),
                       "item":item,"lit":sprites[item["sprite_id"]]}
    if neutral_mode and data is not None:
        house = result[KEYS[0]]
        require(data["house_source"] == {"legacy_key":KEYS[0],"original_rgba_sha256":hashlib.sha256(house["paint"]).hexdigest(),
                "generated_rgba_sha256":hashlib.sha256(house["rgba"]).hexdigest(),"coverage":house["item"]["coverage"],
                "original_pool_sha256":hashlib.sha256(house["original_pool"][2]).hexdigest(),"generated_pool_sha256":hashlib.sha256(house["pool"][2]).hexdigest()}, "Generated silhouette witness differs")
    return result


def verify_source_comparison(material: Path, neutral: Path, data: dict, sources: dict) -> None:
    other = collection_sources(neutral, None)
    worlds = {"textured": load(material / "art/world.json"), "neutral": load(neutral / "art/world.json")}
    expected = []
    changed = covered = 0
    channels = ("albedo", "normal_material", "camera_depth", "emission_mask", "surface_mask", "caster_mask")
    for key in KEYS:
        for channel in channels:
            descriptions = []
            for label, items in (("textured", sources), ("neutral", other)):
                world, item = worlds[label], items[key]["item"]
                binding = next(row for row in world["materials"] if row["id"] == item["material_id"])["channels"][channel]
                descriptions.append(next(row for row in world["textures"] if row["id"] == binding["texture"]))
            a, b = sources[key]["channels"][channel], other[key]["channels"][channel]
            require(all(descriptions[0][field] == descriptions[1][field] for field in ("width", "height", "layout", "mip_levels", "format")) and len(a) == len(b), "Texturing changed paired raw layout")
            exception = key == KEYS[0] and channel == "albedo"
            if exception:
                require(a[3::4] == b[3::4], "Texturing changed geometry alpha")
                for offset in range(0, len(a), 4):
                    covered += a[offset+3] != 0
                    if a[offset:offset+3] != b[offset:offset+3]:
                        require(a[offset+3] == 255, "RGB changed outside opaque authored geometry")
                        changed += 1
            else:
                require(a == b, "Texturing changed five House scalar/geometry channels or painted resident")
            expected.append({"legacy_key": key, "channel": channel, "textured_texture": descriptions[0], "neutral_texture": descriptions[1],
                "bytes": len(a), "textured_sha256": hashlib.sha256(a).hexdigest(), "neutral_sha256": hashlib.sha256(b).hexdigest(),
                "byte_exact": a == b, "rgb_only_exception": exception})
    require(changed > 0 and covered > 0 and data["source_channel_comparison"] == {"channels": expected,
        "generated_covered_pixels": covered, "rgb_changed_pixels": changed, "alpha_exact": True, "five_house_channels_exact": True,
        "six_painted_resident_channels_exact": True, "scope": "actual tight raw channels; exact per-pixel UV requires the separately bound accepted independent CPU oracle"},
        "Native source comparison is not independently reproduced")
    source = sources[KEYS[0]]["item"]["albedo_source"]
    require(source["kind"] == "authored-xyz-triangle-textures-v1" and len(source["source_albedo"]) == 3 and
            other[KEYS[0]]["item"]["albedo_source"]["kind"] == "authored-xyz-triangle-materials-v1", "Actual three-texture/old profile sources differ")
    for row in source["source_albedo"]:
        raw = (material / row["path"]).read_bytes()
        require(1 <= row["width"] <= 1024 and 1 <= row["height"] <= 1024 and row["bytes"] == len(raw) == row["width"] * row["height"] * 3 and
                row["sha256"] == hashlib.sha256(raw).hexdigest() and row["encoding"] == "tight-rgb8-srgb" and row["sampler"] == "nearest-clamp-v1",
                "Delivered authored RGB source layout/hash differs")


def mask_at(source: dict, bounds: list, point: list, original: bool = False) -> list[int]:
    x, y, w, h = bounds
    px, py = point
    if px < x or py < y or px >= f32(x + w) or py >= f32(y + h):
        return [-1, -1, -1]
    (mw, mh), values, _ = source["original_pool" if original else "pool"]
    cx = min(mw - 1, int(f32(f32(f32(px - x) / w) * mw)))
    cy = min(mh - 1, int(f32(f32(f32(py - y) / h) * mh)))
    return [cx, cy, values[cy * mw + cx]]


def query(source: dict, row: dict, zoom: float) -> None:
    entry, rgba = source["entry"], source["rgba"]
    _, _, sw, sh = entry["rect"]
    pivot = [f32(v) for v in entry["pivot"]]
    reference = f32(entry.get("reference_width", sw))
    mask_size, pooled, coverage = source["pool"]
    require(row["crop"] == entry["rect"] and row["reference_width"] == reference and row["pivot"] == pivot and
            row["mask_size"] == mask_size and row["crop_rgba_sha256"] == hashlib.sha256(rgba).hexdigest() and
            row["pooled_alpha_sha256"] == hashlib.sha256(pooled).hexdigest() and
            row["pooled_coverage_sha256"] == hashlib.sha256(coverage).hexdigest() and
            row["original_rgba_sha256"] == hashlib.sha256(source["paint"]).hexdigest() and
            row["original_pooled_coverage_sha256"] == hashlib.sha256(source["original_pool"][2]).hexdigest(), "Actual query mask identities differ")
    building = row["building"]
    require(row["key"] == (KEYS[0] if building else KEYS[1]) and row["entity_id"] == (2 if building else 1), "Normal query entity differs")
    unzoomed = f32(76. * f32(2.1)) if building else 39.
    width = f32(f32(unzoomed * zoom) * f32(sw / reference))
    height = f32(width * f32(sh / sw))
    ground = projected_ground([26., 22.] if building else [21., 27.], zoom, 15. if building else 6.)
    close_values(row["ground"], ground)
    close_values(row["bounds"], [f32(ground[0] - f32(width * pivot[0])), f32(ground[1] - f32(height * pivot[1])), width, height])
    close_values([row["requested_width"], row["geometry_scale"]], [f32(unzoomed * zoom), f32(f32(unzoomed * 4.) / reference)], .000001)
    anchor = [2., -2., f32(-15. / 29.)] if building else [-3., 3., f32(-6. / 29.)]
    close_values(row["expected_render_anchor"], anchor, .000001)
    x, y, w, h = row["bounds"]
    corners = [[x, y], [f32(x + w), y], [f32(x + w), f32(y + h)], [x, f32(y + h)]]
    for key in ("baseline", "mixed"):
        quad = row[key]
        require(len(quad["vertices"]) == 4 and bool(quad["material_index"]) is (key == "mixed"), "Query draw role differs")
        for vertex, point in zip(quad["vertices"], corners, strict=True):
            close_values(vertex[:2], point)
        if key == "mixed":
            require([v[2:4] for v in quad["vertices"]] == [[0., 0.], [1., 0.], [1., 1.], [0., 1.]] and
                    all(v[4:7] == [255, 255, 255] for v in quad["vertices"]), "Typed albedo received a legacy RGB grade")
            surface = quad["surface"]
            close_values(surface["source_size_pivot"], [sw, sh, f32(pivot[0] * sw), f32(pivot[1] * sh)], .000001)
            # The ordinary plate ground is projected and rounded as float32,
            # then View recovers Z from that rounded ground. Derive the same
            # source contract from independent fixture projection, preserving
            # the original tolerance and separate ideal-anchor witness check.
            plane_y = f32(f32(f32(anchor[0] + anchor[1]) * 29.) * .5)
            recovered_z = f32(f32(plane_y - f32(f32(ground[1] - f32(1080. * f32(.48))) / zoom)) / 29.)
            close_values(surface["anchor_export"], [anchor[0], anchor[1], recovered_z, 4.], .000001)
            close_values(surface["depth_scale"][:3], [*source["lit"]["depth_range"], row["geometry_scale"]], .000001)
        else:
            rx, ry = entry["rect"][:2]
            pw, ph = source["page_size"]
            u, v, du, dv = f32(rx / pw), f32(ry / ph), f32(sw / pw), f32(sh / ph)
            for vertex, uv in zip(quad["vertices"], [[u, v], [f32(u + du), v], [f32(u + du), f32(v + dv)], [u, f32(v + dv)]], strict=True):
                close_values(vertex[2:4], uv, .000001)
    require(len(row["samples"]) == 2 and [s["expected_pick"] for s in row["samples"]] == [True, False], "Covered/clear controls are vacuous")
    for sample in row["samples"]:
        point = sample["point"]
        sx = math.floor(f32(f32(f32(point[0] - x) * sw) / w))
        sy = math.floor(f32(f32(f32(point[1] - y) * sh) / h))
        require(0 <= sx < sw and 0 <= sy < sh and sample["raw_source_pixel"] == [sx, sy] and
                sample["raw_alpha"] == rgba[(sy * sw + sx) * 4 + 3], "Raw query sample differs")
        generated, original = mask_at(source, row["bounds"], point), mask_at(source, row["bounds"], point, True)
        require(sample["mask_cell"] == generated[:2] and sample["max_alpha"] == generated[2] and
                sample["mixed_pick"] is sample["expected_pick"] is (generated[2] >= 64) and
                sample["baseline_pick"] is sample["baseline_expected_pick"] is (original[2] >= 64), "Actual SDL selection differs from source alpha")
    disagreement = row["silhouette_disagreement"]
    if source["rgba"] == source["paint"]:
        require(disagreement is None, "Painted mode claims new silhouette")
    else:
        different = sum(a != b for a, b in zip(source["pool"][2], source["original_pool"][2]))
        generated = mask_at(source, row["bounds"], disagreement["point"])
        original = mask_at(source, row["bounds"], disagreement["point"], True)
        require(disagreement["different_pooled_cells"] == different > 0 and disagreement["generated_mask"] == generated and
                disagreement["original_mask"] == original and disagreement["generated_pick"] is (generated[2] >= 64) and
                disagreement["original_pick"] is (original[2] >= 64) and disagreement["generated_pick"] is not disagreement["original_pick"],
                "Changed neutral silhouette pick control is vacuous")
    require(row["half_open_baseline_pick"] is False and row["half_open_mixed_pick"] is False and
            row["half_open_point"][0] >= f32(x + w), "Right half-open boundary was not exercised")
    close_values(row["camera_origin"], projected_ground([24., 24.], zoom, 0.))


def outline(row: dict, bounds: list, radius: float) -> None:
    require(row["vertex_count"] == 32 and row["index_count"] == 48 and len(row["quads"]) == 8 and
            len(row["texture"]) == 2 and all(type(v) is int and v > 0 for v in row["texture"]), "Outline/cache witness is vacuous")
    close_values(row["bounds"], bounds)
    close_values([row["radius"]], [radius], .000001)
    draw = row["draw"]
    require(len(draw) == 14 and draw[:2] == [1, 1] and draw[2:4] == row["texture"] and draw[5] == 48 and
            draw[10:] == [False, False, 0, 0], "Typed outline is not one plain WorldOrdered command")
    unique = set()
    offsets = [(-1, -1), (0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0)]
    for index, (quad, direction) in enumerate(zip(row["quads"], offsets, strict=True)):
        require(quad["offset"] == list(direction) and quad["first_index"] == draw[4] + index * 6, "Outline offset order differs")
        a = quad["indices"][0]
        require(quad["indices"] == [a, a + 1, a + 2, a, a + 2, a + 3], "Outline indices differ")
        unique.update(range(a, a + 4))
        x = f32(bounds[0] + f32(direction[0] * radius))
        y = f32(bounds[1] + f32(direction[1] * radius))
        corners = [[x, y], [f32(x + bounds[2]), y], [f32(x + bounds[2]), f32(y + bounds[3])], [x, f32(y + bounds[3])]]
        for vertex, point, uv in zip(quad["vertices"], corners, [[0., 0.], [1., 0.], [1., 1.], [0., 1.]], strict=True):
            close_values(vertex[:2], point)
            require(vertex[2:4] == uv and vertex[4:] == [69, 210, 247, 235], "Outline alpha/UV/color differs")
    require(len(unique) == 32, "Outline shared or missing vertices")


def frame_contract(capture: dict, captured: bool = True) -> None:
    frame, telemetry = capture["frame_contract"], capture["telemetry"]
    counters = ("vertex_count", "index_count", "command_count", "sprite_material_count", "companion_count",
                "neutral_companion_count", "non_neutral_companion_count", "typed_command_count", "plain_geometry_command_count", "text_command_count")
    require(all(type(frame[name]) is int and frame[name] >= 0 for name in counters) and type(frame["hdr"]) is bool and
            frame["content"] in (0, 1) and frame["command_count"] == frame["typed_command_count"] +
            frame["plain_geometry_command_count"] + frame["text_command_count"], "Frame counters/roles differ")
    removed = capture["id"] == "textured-body-default-house-removed"
    require(frame["typed_command_count"] == len(frame["typed_roles"]) == frame["sprite_material_count"] - int(removed) and
            frame["non_neutral_companion_count"] == 0 and frame["neutral_companion_count"] == frame["companion_count"] and
            frame["companion_count"] == (frame["vertex_count"] if frame["hdr"] else 0), "Companion stream differs")
    expected = 3 if capture["id"] == "textured-two-houses-selected" else 1 if capture["id"] in {"textured-variants", "textured-winter"} else 2
    zero = {"textured-menu-first", "textured-original-default", "textured-mine-fallback", "textured-mine-legacy", "textured-authority-unavailable"}
    if capture["id"] in zero:
        expected = 0
    require(frame["sprite_material_count"] == expected or capture["id"] == "textured-animation-fallback" and
            frame["sprite_material_count"] in (1, 2), "Exact material subset differs")
    require(frame["hdr"] is (frame["sprite_material_count"] > 0), "Mapped HDR route differs")
    neutral = [[[0., 0., 0.], [0., 0., 1.]]] * frame["companion_count"]
    require(frame["companion_sha256"] == hashlib.sha256(canonical(neutral)).hexdigest(), "Whole neutral companion digest differs")
    require(frame["content"] == (1 if capture["id"] == "textured-menu-first" else 0), "FrameContent differs")
    if capture["id"] == "textured-authority-unavailable":
        require(all(frame[k] == 0 for k in counters), "Unavailable authority invented World")
    else:
        require(all(frame[k] > 0 for k in ("vertex_count", "index_count", "command_count")), "Ready geometry is vacuous")
    for role in frame["typed_roles"]:
        draw = role["draw"]
        require(len(draw) == 14 and draw[:4] == [1, 1, 0, 0] and draw[5] == 6 and draw[10:12] == [False, False] and
                1 <= draw[12] <= frame["sprite_material_count"] and draw[13] == 0 and
                len(role["referenced_vertices"]) == 4 and all(v[1:4] == [255, 255, 255] for v in role["referenced_vertices"]),
                "Typed role/RGB was corrupted")
    require(telemetry["submitted"] is True and telemetry["capture"] is captured and telemetry["gpu_execution_ms"] is None and
            telemetry["material_draws"] == frame["typed_command_count"] and telemetry["surface_draws"] == telemetry["ground_draws"] == 0 and
            telemetry["vertices"] >= frame["vertex_count"] and telemetry["indices"] >= frame["index_count"] and
            telemetry["graph_passes"] == ([4] if frame["content"] == 1 else []) and
            telemetry["graph_pass_count"] == (1 if frame["content"] == 1 else 0) and
            all(telemetry[name] == 0 for name in ("occluders", "occluder_upload_bytes", "contact_casters", "contact_draws",
                "contact_mask_binds", "water_draws", "reflection_draws")), "Actual graph/telemetry differs")


def cold_aliases(output: Path, material: Path) -> dict[Path, Path]:
    metadata = load(material / "art/material-collection.json")
    house = next(item for item in metadata["items"] if item["legacy_key"] == KEYS[0])
    source = house["albedo_source"]
    return {output / "invalid-root-reparse": material,
            output / "invalid-notice-reparse/notice-alias": (material / source["provenance"]["notice_path"]).parent,
            output / "invalid-palette-internal-reparse/palette-alias": (material / source["palette_path"]).parent}


def verify_native(build: Path, objects: Path, kit: Path, ui: Path, material: Path, neutral: Path, painted: Path, cpu: Path, profile: str,
                  output: Path, report: dict, checked: set[Path]) -> dict:
    execution = load(output / "native-execution.json")
    require(execution["schema"] == 1 and execution["profile"] == profile and execution["accepted"] is True and
            execution["actual_subprocess"] is True and execution["signed_evidence"] is False and execution["exit_code"] == 0 and
            execution["native_created_output"] is True and execution["bound_inputs_unchanged"] is True and execution["integrity_errors"] == [], "Actual native wrapper rejected")
    roots = {"repository": REPOSITORY, "build_root": build, "object_root": objects, "kit_root": kit,
             "ui_kit_root": ui, "original_art_root": REPOSITORY / "assets", "material_collection_root": material,
             "neutral_compatibility_root": neutral, "painted_compatibility_root": painted, "cpu_proof_report": cpu, "output_root": output}
    require(all(Path(execution[key]) == value for key, value in roots.items()), "Native root linkage differs")
    exe = build / "GameViewTexturedMaterialProbe.exe"
    argv = [str(exe), profile, str(build / "assets"), str(REPOSITORY / "assets"), str(material), str(neutral), str(painted), str(ui), str(cpu), str(output)]
    require(execution["actual_argv"] == argv and execution["arguments"] == argv[1:] and Path(execution["executable"]) == exe,
            "Actual profile/argv differs")
    for field, path in (("runner", REPOSITORY / "tools/run_gameview_textured_material_isolated.ps1"),
                        ("build_report", build / "build-report.json"), ("artifact_storage", build / "artifact-storage.json"),
                        ("input_final_binding", output / "input-final-hashes.json"),
                        ("combined_output_binding", output / "execution-output.txt")):
        require(Path(execution[field]["path"]) == path, "Selected wrapper binding differs")
        hash_row(execution[field], path)
    source_by_name = {row["relative_path"]: row for row in report["source_provenance"]}
    runner = source_by_name["tools/run_gameview_textured_material_isolated.ps1"]
    require(execution["runner"]["bytes"] == runner["bytes"] and execution["runner"]["sha256"].upper() == runner["sha256"].upper(),
            "Runner differs from compiled closure")
    bound_rows(execution, checked)
    final = load(output / "input-final-hashes.json")
    require(set(final) == {"before", "after"} and len(final["before"]) == len(final["after"]) > 0 and
            len({row["path"].casefold() for row in final["before"]}) == len(final["before"]), "Native full input ledger differs")
    before_by_path = {Path(row["path"]): row for row in final["before"]}
    require(set(before_by_path) == {Path(row["path"]) for row in final["after"]}, "Native final input membership differs")
    for after in final["after"]:
        prior = before_by_path[Path(after["path"])]
        require(set(prior) == {"path", "bytes", "sha256"} and set(after) == {"path", "bytes", "sha256", "after_bytes", "after_sha256", "unchanged"} and
                after["unchanged"] is True and prior["bytes"] == after["bytes"] == after["after_bytes"] and
                prior["sha256"] == after["sha256"] == after["after_sha256"], "Native input changed")
        no_reparse(Path(prior["path"]))
        hash_row(prior, Path(prior["path"]))
        checked.add(Path(prior["path"]))
    required_paths = {path for path in checked if path != output and output not in path.parents and
                      path.name not in {"native-execution.json", "execution-output.txt", "input-final-hashes.json"}}
    # The wrapper additionally binds complete artifact and protected input trees.
    require(required_paths <= set(before_by_path), "Native ledger omitted a build-bound file")
    for key, root in (("build", build), ("objects", objects), ("control", kit), ("ui", ui),
                      ("original", REPOSITORY / "assets"), ("material", material), ("neutral", neutral), ("painted", painted)):
        require(indexed_inventory(execution["protected_trees"][key]) == indexed_inventory(inventory(root)), "Native tree membership changed")
    preferences = execution["ordinary_preferences"]
    expected_preferences = Path(os.environ["APPDATA"]) / "Ribterra/Ribterra"
    require(Path(preferences["root"]) == expected_preferences and preferences["existed_before"] is preferences["existed_after"] is expected_preferences.is_dir() and
            preferences["existed_before"] is True and preferences["existed_after"] is True and len(preferences["files_before"]) == len(preferences["files_after"]) == 36 and
            indexed_inventory(preferences["files_before"]) == indexed_inventory(preferences["files_after"]) ==
            indexed_inventory(inventory(expected_preferences) if expected_preferences.is_dir() else []),
            "Ordinary preferences changed")
    storage = execution["storage"]
    require(storage == {"maximum_bytes_each": MAXIMUM, "minimum_free_bytes": MINIMUM_FREE, "aggregate_limit_claimed": False,
            "build_bytes": total(inventory(build)), "object_bytes": total(inventory(objects)),
            "build_plus_object_bytes": total(inventory(build)) + total(inventory(objects))}, "Wrapper storage sum differs")
    require(execution["claims"] == {"fps": None, "gpu_execution_ms": None, "production_reference": False,
            "all_eight_masters": False, "normal_main": False, "full_engine": False}, "Wrapper overstates delivery")
    aliases = cold_aliases(output, material) if profile == "cold-limits" else {}
    actual = inventory(output, aliases)
    recorded = execution["native_output_before_runner_evidence"]
    extras = {"native-execution.json", "execution-output.txt", "input-final-hashes.json"}
    native_rows = [row for row in actual if row["relative_path"] not in extras]
    require(set(recorded) == {"file_count", "total_bytes", "files"} and recorded["file_count"] == len(native_rows) and
            recorded["total_bytes"] == total(native_rows) and indexed_inventory(recorded["files"]) == indexed_inventory(native_rows) and
            total(actual) <= MAXIMUM, "Exact retained profile inventory/budget differs")
    native = load(output / "textured-material-report.json")
    require(set(native) == {"schema", "profile", "complete", "deterministic", "measured"} and native["schema"] == 1 and
            native["profile"] == profile and native["complete"] is True and set(native["measured"]) == {"total_ms"} and
            type(native["measured"]["total_ms"]) in (int, float) and math.isfinite(native["measured"]["total_ms"]) and native["measured"]["total_ms"] >= 0,
            "Actual profile report incomplete or timing scope differs")
    data = native["deterministic"]
    require(native_inventory_index(data["output_inventory_excluding_report"], "output") ==
            native_inventory_index(lower_rows([row for row in native_rows if row["relative_path"] != "textured-material-report.json"]), "current output"),
            "Probe native membership/bytes/SHA differ")
    trees = {"runtime": build / "assets", "original": REPOSITORY / "assets", "collection": material, "neutral": neutral, "painted": painted, "ui": ui}
    expected_protected = {key: lower_rows(inventory(root)) for key, root in trees.items()}
    require(type(data["protected_before"]) is dict and
            set(data["protected_before"]) == set(expected_protected) and
            data["protected_before"] == data["protected_after"], "Probe before/after protected bytes differ")
    for label, current_rows in expected_protected.items():
        require(native_inventory_index(data["protected_before"][label], label) ==
                native_inventory_index(current_rows, "current " + label),
                "Probe current protected membership/bytes/SHA differ: " + label)
    expected_names = set(SAVES if profile == "positive" else SAVES[:1]) | {"textured-material-report.json"}
    if profile == "positive":
        expected_names |= {name + ".png" for name in CAPTURES}
    else:
        expected_names |= cold_files(output, material, data, aliases, profile)
    require({row["relative_path"] for row in native_rows} == expected_names, "Unexpected profile file closure")
    return native


def cold_files(output: Path, material: Path, data: dict, aliases: dict[Path, Path], profile: str) -> set[str]:
    rows = data["cold"]["rejections"]
    require(data["cold"]["profile"] == profile and data["cold"]["variant_ids"] == list(COLD_CASES[profile]) and
            [row["id"] for row in rows] == list(cold_ids(profile)) and len(rows) == (15 if profile == "cold-source" else 14) and
            data["cold"]["variant_catalog_count"] == 12 and data["cold"]["captures"] == 0 and data["cold"]["registered_count"] == 2 and
            data["cold"]["saved_world_exact"] is True and data["captures"] == [] and data["zoom_cases"] == [] and data["capture_limit"] == 0,
            "Cold shard coverage or GPU scope differs")
    expected_alias_rows = [{"path": path.relative_to(output).as_posix(), "target": target.as_posix(), "owned": True,
                           "target_exact": True, "followed_in_inventory": False} for path, target in sorted(aliases.items())]
    require(data["retained_junctions"] == expected_alias_rows, "Retained exact junction ledger differs")
    expected = set()
    for row in rows:
        label = "palette-reread-oversize" if row["id"].startswith("palette-reread-oversize-") else row["id"]
        wanted = COLD_DIAGNOSTICS[label]
        require(type(row["error"]) is str and wanted == row["diagnostic_contains"] and wanted in row["error"] and
                row["frame_camera_pick_save_exact"] is True and row["before_frame_sha256"] == row["after_frame_sha256"], "Cold rejection lost exact state or missed its intended guard")
        if "root" not in row:
            require(profile == "cold-source" and row["id"] in PROFILE_IDS, "Missing cold fixture root")
            continue
        root = output / row["root"]
        require(row["root"] == "invalid-" + label, "Cold fixture root/ID differs")
        if label == "root-reparse":
            require(Path(row["reparse_target"]) == material and root.resolve(strict=True) == material, "Root alias target differs")
            continue
        files = inventory(root, aliases)
        require(native_inventory_index(row["files"], row["id"]) == native_inventory_index(lower_rows(files), "current " + row["id"]), "Cold physical fixture not fully bound")
        expected |= {row["root"] + "/" + value["relative_path"] for value in files}
        if label == "palette-reread-oversize":
            require(row["operation"] == row["id"].rsplit("-", 1)[1] and row["initial_constructor_passed"] is True and row["copied_profile_preserved"] is True,
                    "Reread positive precursor/copy missing")
        else:
            require(row["constructor_inside_try"] is True, "Cold constructor outside rejection transaction")
    verify_cold_variants(output, material, aliases, profile)
    return expected


def verify_cold_variants(output: Path, material: Path, aliases: dict[Path, Path], profile: str) -> None:
    baseline = {row["relative_path"]: Path(row["path"]).read_bytes() for row in inventory(material)}
    accepted_metadata, accepted_world, accepted_manifest = (json.loads(baseline[name]) for name in
        ("art/material-collection.json", "art/world.json", "manifest.json"))
    hi = next(i for i, item in enumerate(accepted_metadata["items"]) if item["legacy_key"] == KEYS[0])
    mi = next(i for i, item in enumerate(accepted_metadata["items"]) if item["legacy_key"] == KEYS[1])
    palette_path = accepted_metadata["items"][hi]["albedo_source"]["palette_path"]
    def bind(metadata: dict, world: dict, index: int) -> None:
        item = metadata["items"][index]
        item["source_hash"] = source_hash(source_record(item))
        next(row for row in world["materials"] if row["id"] == item["material_id"])["source_hash"] = item["source_hash"]
    def substitute(value: object, old: str, new: str) -> object:
        if type(value) is str: return value.replace(old, new)
        if type(value) is list: return [substitute(child, old, new) for child in value]
        if type(value) is dict: return {key: substitute(child, old, new) for key, child in value.items()}
        return value
    for case in COLD_CASES[profile]:
        if case == "root-reparse": continue
        root = output / ("invalid-" + case)
        actual_files = {row["relative_path"]: Path(row["path"]).read_bytes() for row in inventory(root, aliases)}
        em, ew, manifest = copy.deepcopy(accepted_metadata), copy.deepcopy(accepted_world), copy.deepcopy(accepted_manifest)
        house, man = em["items"][hi], em["items"][mi]
        ep = json.loads(baseline[palette_path])
        palette_id = house["namespace"] + ".palette"
        source = house["albedo_source"]["source_albedo"][0]
        source_id = house["namespace"] + ".source_albedo." + source["id"]
        source_path = source["path"]
        expected_payload = dict(baseline)
        changed_palette = changed_source = False
        declared, hashes = {}, {}
        if case == "schema": ew["schema"] = 3
        elif case == "reference_width": man["registration"]["reference_width"] = 139.; changed_source = True
        elif case == "decoded_crop_hash": man["registration"]["decoded_crop_sha256"] = "0" * 64; changed_source = True
        elif case == "legacy_key":
            old_sprite = man["sprite_id"]
            man["legacy_key"] = "resident.man.walk.unknown"; man["sprite_id"] = "dynamic.resident.man.walk.unknown"
            next(row for row in ew["lit_sprites"] if row["id"] == old_sprite)["id"] = man["sprite_id"]
        elif case == "albedo_byte":
            texture_id = next(row for row in ew["materials"] if row["id"] == man["material_id"])["channels"]["albedo"]["texture"]
            texture = next(row for row in ew["textures"] if row["id"] == texture_id)
            blob = bytearray(baseline[texture["path"]]); blob[0] ^= 1
            expected_payload[texture["path"]] = bytes(blob); texture["sha256"] = hashlib.sha256(blob).hexdigest()
        elif case == "neutral-extra": house["albedo_source"]["extra"] = True; changed_source = True
        elif case == "authored-provenance": house["albedo_source"]["provenance"]["author"] = "Different author"; changed_source = True
        elif case == "coverage": house["coverage"]["original_alpha_samples"] = 0
        elif case == "generated-sha": house["generated_albedo_sha256"] = "0" * 64; changed_source = True
        elif case == "source-missing": manifest["assets"] = [row for row in manifest["assets"] if row["id"] != source_id]
        elif case in {"source-sha", "source-dimensions", "source-bytes"}:
            texture = next(row for row in ep["textures"] if row["id"] == source["id"])
            field, value = ("sha256", "0" * 64) if case == "source-sha" else ("width", 1025) if case == "source-dimensions" else ("bytes", source["bytes"] + 1)
            source[field] = texture[field] = value
            if case == "source-sha": hashes[source_id] = value
            if case == "source-bytes": declared[source_id] = value
            changed_palette = True
        elif case == "uv-range": ep["triangle_materials"][0]["uv"][0][0] = 1.25; changed_palette = True
        elif case == "material-assignment": ep["triangle_materials"][0]["material_id"] = "unknown.material"; changed_palette = True
        elif case in {"palette-declared-oversize", "palette-actual-oversize"}:
            expected_payload[palette_path] = baseline[palette_path] + b" " * (524289 - len(baseline[palette_path]))
            hashes[palette_id] = house["albedo_source"]["palette_sha256"]
            if case == "palette-actual-oversize": declared[palette_id] = 524288
        elif case == "palette-reread-oversize":
            expected_payload["retained-before-palette.json"] = baseline[palette_path]
            expected_payload[palette_path] = b" " * 524289
        elif case == "source-physical-oversize":
            expected_payload[source_path] = baseline[source_path] + b"\0" * (33554433 - len(baseline[source_path]))
            declared[source_id], hashes[source_id] = source["bytes"], source["sha256"]
        elif case == "source-role-count":
            while len(house["albedo_source"]["source_albedo"]) < 5: house["albedo_source"]["source_albedo"].append(copy.deepcopy(source))
            while len(ep["textures"]) < 5: ep["textures"].append(copy.deepcopy(ep["textures"][0]))
            changed_palette = True
        elif case == "notice-reparse":
            old_notice = house["albedo_source"]["provenance"]["notice_path"]
            new_notice = "notice-alias/" + Path(old_notice).name
            house["albedo_source"]["provenance"]["notice_path"] = ep["draft_notice"]["path"] = new_notice
            for row in manifest["assets"]:
                if row["notice"] == old_notice: row["notice"] = new_notice
            changed_palette = True
        elif case == "palette-internal-reparse":
            new_path = "palette-alias/" + Path(palette_path).name
            house["albedo_source"]["palette_path"] = new_path
            next(row for row in manifest["assets"] if row["id"] == palette_id)["path"] = new_path
            changed_source = True
        elif case == "collection-role-count":
            old, source_house = house["namespace"], copy.deepcopy(house)
            base_material = next(row for row in accepted_world["materials"] if row["id"] == house["material_id"])
            base_sprite = next(row for row in accepted_world["lit_sprites"] if row["id"] == house["sprite_id"])
            for index in range(2):
                new = old + ".limit" + str(index)
                item = substitute(source_house, old, new)
                item["legacy_key"] = "building.house.limit" + str(index); item["sprite_id"] = "dynamic." + item["legacy_key"]
                cloned_palette_path = "art/materials/" + new + "/source-palette.json"
                item["albedo_source"]["palette_path"] = cloned_palette_path
                em["items"].append(item); ew["materials"].append(substitute(base_material, old, new))
                sprite = substitute(base_sprite, old, new); sprite["id"] = item["sprite_id"]; ew["lit_sprites"].append(sprite)
                ew["textures"].extend(substitute(row, old, new) for row in accepted_world["textures"] if row["id"].startswith(old + "."))
                for row in accepted_manifest["assets"]:
                    if row["id"].startswith(old + "."):
                        cloned = substitute(row, old, new)
                        if row["id"] == old + ".palette": cloned["path"] = cloned_palette_path
                        manifest["assets"].append(cloned)
                        expected_payload[cloned["path"]] = baseline[row["path"]]
                bind(em, ew, len(em["items"]) - 1)
            em["item_count"] = 4
        else: raise AssertionError("Unvalidated cold case")
        if changed_palette:
            require(json.loads(actual_files[palette_path]) == ep, "Cold palette changed beyond intended mutation")
            house["albedo_source"]["palette_sha256"] = hashlib.sha256(actual_files[palette_path]).hexdigest()
            changed_source = True
        elif case not in {"palette-declared-oversize", "palette-actual-oversize", "palette-reread-oversize"}:
            require(actual_files[palette_path] == baseline[palette_path], "Unrelated palette bytes changed")
        if changed_source:
            bind(em, ew, hi); bind(em, ew, mi)
        require(load(root / "art/material-collection.json") == em and load(root / "art/world.json") == ew, "Cold metadata/world mutation differs")
        require(set(actual_files) == set(expected_payload), "Cold physical payload membership differs")
        for name, blob in expected_payload.items():
            if name in {"manifest.json", "art/material-collection.json", "art/world.json"} or name == palette_path and changed_palette: continue
            require(actual_files[name] == blob, "Cold unrelated payload changed: " + name)
        for row in manifest["assets"]:
            path = row["path"]
            blob = baseline[palette_path] if case in {"palette-reread-oversize", "palette-internal-reparse"} and row["id"] == palette_id else actual_files[path]
            row["bytes"] = declared.get(row["id"], len(blob)); row["sha256"] = hashes.get(row["id"], hashlib.sha256(blob).hexdigest())
        manifest["assets"].sort(key=lambda row: row["id"])
        actual_manifest = load(root / "manifest.json")
        manifest_content(actual_manifest)
        manifest["content_sha256"] = actual_manifest["content_sha256"]
        require(actual_manifest == manifest, "Cold rebound manifest differs")


def verify_direct(data: dict, sources: dict[str, dict]) -> None:
    direct = data["direct"]
    require([row["id"] for row in direct["api_controls"]] == list(API_IDS), "Direct outline control set differs")
    for row in direct["api_controls"]:
        require(row["frame_exact"] is True and row["cache_exact"] is True, "Direct API lost frame/cache")
        if row["id"] == "unknown-id-alpha-zero":
            require(row["exception_type"] == "out_of_range" and type(row["error"]) is str and row["error"],
                    "Unknown ID was not rejected before the zero-alpha no-op")
        elif row["id"] not in {"alpha-zero", "radius-zero"}:
            require(type(row["error"]) is str and row["error"] and row["diagnostic_contains"] in row["error"], "Direct API hit another failure")
    require([row["id"] for row in direct["invalid_limits"]] == ["hit-limit-above-maximum", "outline-limit-above-maximum"] and
            all(row["cache_exact"] is True and row["error"] == "Game art cache caps exceed retained memory limits" for row in direct["invalid_limits"]),
            "Invalid cache-limit controls differ")
    maximum_hit, maximum_outline = 16 * 1024**2, 20 * 1024**2
    legacy, typed = direct["legacy"], direct["typed"]
    for counters in (legacy, typed):
        require(set(counters) == {"hit_mask_bytes", "hit_mask_count", "outline_texture_bytes", "outline_sprite_count"} and
                all(type(value) is int and value >= 0 for value in counters.values()), "Invalid retained cache counters")
    world = load(REPOSITORY / "assets/art/world.json")
    dimensions, outline_dimensions, original_ids = [], [], []
    for page in world["pages"]:
        for sprite in page["sprites"]:
            key = sprite["id"]
            original_ids.append(key)
            # GameArt retains CPU coverage for buildings and residents;
            # only buildings receive an eagerly loaded GPU outline mask.
            if not key.startswith(("building.", "resident.")):
                continue
            w, h = sprite["rect"][2:]
            scale = min(1., f32(192. / max(w, h)))
            size = [max(1, math.floor(f32(w * scale) + .5)), max(1, math.floor(f32(h * scale) + .5))]
            dimensions.append(size)
            if key.startswith("building."):
                outline_dimensions.append(size)
    require(len(original_ids) == len(set(original_ids)) == 987 and legacy == {
                "hit_mask_bytes": sum(w * h for w, h in dimensions), "hit_mask_count": len(dimensions),
                "outline_texture_bytes": 4 * sum(w * h for w, h in outline_dimensions),
                "outline_sprite_count": len(outline_dimensions)}, "Original source-role retained-cache counters differ")
    typed_hit = sum(source["pool"][0][0] * source["pool"][0][1] for source in sources.values())
    house = sources[KEYS[0]]
    house_pixels = math.prod(house["pool"][0])
    require(typed == {"hit_mask_bytes": typed_hit, "hit_mask_count": 2, "outline_texture_bytes": house_pixels * 4,
                      "outline_sprite_count": 1} and direct["maximum_hit_mask_bytes"] == maximum_hit and direct["maximum_outline_bytes"] == maximum_outline and
            direct["remaining_hit_mask_bytes"] == maximum_hit - legacy["hit_mask_bytes"] and
            direct["remaining_outline_bytes"] == maximum_outline - legacy["outline_texture_bytes"] and
            legacy["hit_mask_bytes"] + typed_hit <= maximum_hit and legacy["outline_texture_bytes"] + typed["outline_texture_bytes"] <= maximum_outline,
            "Combined published cache totals differ")
    require(direct["repeat_frame_exact"] is direct["repeat_cache_exact"] is direct["remaining_hit_cap_rejected"] is direct["zero_outline_cap_rejected"] is True and
            "hit-mask" in direct["remaining_hit_cap_error"] and direct["memory_scope"] == "Published retained caches; candidate coexistence/transient decode separate",
            "Lazy-cap/load/cache atomicity scope differs")
    first = direct["first_outline"]
    entry = house["entry"]
    sw, sh = entry["rect"][2:]
    requested = f32(f32(76. * f32(2.1)) * f32(1.3))
    w, h = f32(requested * f32(sw / f32(entry.get("reference_width", sw)))), 0.
    h = f32(w * f32(sh / sw))
    bounds = [f32(960. - f32(w * f32(entry["pivot"][0]))), f32(540. - f32(h * f32(entry["pivot"][1]))), w, h]
    outline(first, bounds, f32(1.56))
    failure = direct["sampled_candidate_failure"]
    require(failure == {"error": "Injected sampled texture candidate creation failure", "legacy_catalog_exact": True,
            "frame_exact": True, "cache_exact": True, "scope": "Renderer-injected sampled candidate failure; physical GPU OOM/plain outline fault OPEN"},
            "Injected sampled-candidate evidence differs")
    mirror, man = direct["legacy_mirror"], sources[KEYS[1]]
    require(mirror["pool_sha256"] == hashlib.sha256(man["pool"][2]).hexdigest() and mirror["quad"]["material_index"] == 0 and
            mirror["quad"]["vertices"][0][2] > mirror["quad"]["vertices"][1][2] and
            [sample["expected_pick"] for sample in mirror["samples"]] == [True, False] and
            mirror["scope"] == "Direct original GameArt API; full canonical catalog normal mirrored route untriggered", "Legacy mirror scope differs")
    for sample in mirror["samples"]:
        x, y, w, _ = mirror["bounds"]
        close_values(sample["reflected_point"], [f32(f32(x + w) - f32(sample["source_point"][0] - x)), sample["source_point"][1]], .0001)
        expected = mask_at(man, mirror["bounds"], sample["source_point"])
        require(sample["source_mask"] == expected and sample["actual_pick"] is sample["expected_pick"] is (expected[2] >= 64), "Mirrored max-pool selection differs")


def verify_positive(output: Path, data: dict, sources: dict[str, dict]) -> dict:
    from PIL import Image, ImageChops
    require([row["id"] for row in data["captures"]] == list(CAPTURES) and data["capture_limit"] == 13 and
            data["saved_worlds_preserved"] is True, "Positive captures/saves differ")
    images, by_id = {}, {}
    for row in data["captures"]:
        path = output / (row["id"] + ".png")
        require(row["file"] == path.name and row["bytes"] == path.stat().st_size and row["sha256"] == digest(path) and
                row["width"] == 1920 and row["height"] == 1080, "Capture file binding differs")
        with Image.open(path) as image:
            require(image.size == (1920, 1080), "Decoded capture size differs")
            images[row["id"]] = image.convert("RGBA")
        frame_contract(row)
        by_id[row["id"]] = row
    def changed(a: str, b: str, bounds: list | None = None) -> int:
        channels = ImageChops.difference(images[a], images[b]).split()
        mask = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), ImageChops.lighter(channels[2], channels[3]))
        if bounds is not None:
            x, y, w, h = bounds
            box = (max(0, math.ceil(x - .5)), max(0, math.ceil(y - .5)), min(1920, math.ceil(f32(x + w) - .5)), min(1080, math.ceil(f32(y + h) - .5)))
            require(box[0] < box[2] and box[1] < box[3], "Offscreen/vacuous image region")
            mask = mask.crop(box)
        return mask.width * mask.height - mask.histogram()[0]
    # Unselected HUD top/bottom/right panels do not occupy this central scene
    # region. Nonmodal pixels exclude both black and uniform background oracles.
    visible = {}
    for name in ("textured-body-default", "textured-original-default", "textured-neutral-default", "textured-painted-default"):
        pixels = Counter(images[name].crop((320, 200, 1080, 650)).getdata())
        nonmodal = 760 * 450 - max(pixels.values())
        require(len(pixels) > 64 and nonmodal > 10000, "Actual World scene is uniform/vacuous")
        visible[name] = {"unique_rgba": len(pixels), "nonmodal_pixels": nonmodal}
    menu = images["textured-menu-first"]
    require(len(set(menu.convert("RGB").getdata())) == data["ui"]["menu_first"]["unique_rgb"] > 100, "Actual menu pixels are vacuous")
    zooms = data["zoom_cases"]
    require([row["id"] for row in zooms] == ["default", "min", "max"] and len(zooms) == 3, "Zoom controls differ")
    cache = zooms[0]["outline"]["texture"]
    for row, zoom, wheel in zip(zooms, (f32(1.3), f32(.62), 3.), (0., -100., 100.), strict=True):
        require(row["zoom"] == zoom and row["wheel_delta"] == wheel and row["typed_material_draws"] == 2 and row["cache_handle_equal"] is True,
                "Ordinary wheel/clamp control differs")
        query(sources[KEYS[0]], row["house"], zoom)
        query(sources[KEYS[1]], row["resident"], zoom)
        body_id, outline_id = "textured-body-" + row["id"], "textured-outline-" + row["id"]
        for name in ("house", "resident"):
            actual = row[name]["mixed"]
            require(any(role["command"] == actual["command"] and role["draw"] == actual["draw"] for role in by_id[body_id]["frame_contract"]["typed_roles"]),
                    "Queried body is not in the actual captured frame")
        radius = min(2.4, max(1.2, f32(f32(1.2) * zoom)))
        outline(row["outline"], row["house"]["bounds"], radius)
        require(row["outline"]["texture"] == cache and row["body_vs_outline_changed_pixels"] == changed(body_id, outline_id) > 0,
                "Rendered outline/cache control differs")
        frame, selected = by_id[body_id]["frame_contract"], by_id[outline_id]["frame_contract"]
        require(selected["vertex_count"] == frame["vertex_count"] + 32 and selected["index_count"] == frame["index_count"] + 48,
                "Single outline appended another payload")
    for query_name in ("house", "resident"):
        require(changed("textured-original-default", "textured-body-default", zooms[0][query_name]["bounds"]) > 0, "Mapped native route change is vacuous")
    for name, profile_value in (("neutral", 1), ("painted", 0)):
        row = data["old_profiles"][name]
        capture = by_id[f"textured-{name}-default"]
        require(row["read_profile"] == profile_value and row["registered_count"] == 2 and row["capture"] == capture["file"] and
                row["frame_sha256"] == capture["frame_sha256"] and row["frame_contract"] == capture["frame_contract"] and
                row["rgba_sha256"] == hashlib.sha256(images[capture["id"]].tobytes()).hexdigest(), "Old profile actual capture differs")
    old = data["old_profiles"]["neutral"]
    require(old["house_changed_pixels"] == changed("textured-body-default", "textured-neutral-default", zooms[0]["house"]["bounds"]) > 0 and
            old["total_changed_pixels"] == changed("textured-body-default", "textured-neutral-default") > 0, "Textured vs old neutral RGB response is vacuous")
    verify_body_visibility(data["body_visibility"], sources[KEYS[0]], zooms[0]["house"], images, by_id, changed)
    authority = data["authority_cases"]
    require([row["id"] for row in authority] == ["night", "wet", "mine", "unavailable"], "Authority case set differs")
    for row, tick, wet in zip(authority[:2], (21600, 7200), (0., f32(.6005)), strict=True):
        weather = row["weather"]
        require(weather["known"] is True and weather["evaluated_tick"] == weather["calendar_tick"] == tick and weather["offset"] == 0 and
                weather["wet_permille"] == (600 if row["id"] == "wet" else 0) and weather["wet_remainder"] == (14400 if row["id"] == "wet" else 0) and
                weather["snow_permille"] == 0 and type(weather["kind"]) is int and 0 <= weather["kind"] <= 3, "Authority calendar/cover differs")
        hour = f32((6. + tick / 1200.) % 24.)
        close_values([row["hour"], row["wetness"]], [hour, wet], .000001)
        for name, expected in zip(("ambient", "sun_direction", "sun_color_intensity"), lighting(hour, weather["kind"]), strict=True):
            close_values(row[name], expected, .000003)
        require(row["actual_submission"] is True and row["legacy_native_capture"] is False, "Authority submission scope differs")
        if row["id"] == "wet":
            require(row["mixed_native_capture"] is True and row["mixed_vs_day_changed_pixels"] == changed("textured-mixed-wet", "textured-body-default") > 0,
                    "Wet native response is vacuous")
        else:
            require(row["mixed_native_capture"] is False and row["mixed_vs_day_changed_pixels"] is None, "Uncaptured night pixel claim")
        if row["id"] == "wet":
            require(row["legacy_wet_frame_exact"] is True and row["legacy_frame_sha256"] == row["day_legacy_frame_sha256"] == by_id["textured-original-default"]["frame_sha256"] and
                    row["legacy_vs_day_changed_pixels"] == 0 and row["legacy_vs_mixed_changed_pixels"] == changed("textured-original-default", "textured-mixed-wet") > 0,
                    "Wet-only original fallback changed")
        else:
            require(row["legacy_vs_day_changed_pixels"] is row["legacy_vs_mixed_changed_pixels"] is None and row["legacy_wet_frame_exact"] is False,
                    "Uncaptured night legacy pixels were invented")
    mine, unavailable = authority[2:]
    require(mine["pixels_equal"] is None and mine["frame_equal"] is True and mine["actual_submissions"] == 2 and mine["hdr"] is False and mine["typed_material_draws"] == 0 and
            mine["mixed_frame_sha256"] == mine["baseline_frame_sha256"],
            "Mine original fallback differs")
    require(unavailable["remote"] is True and all(unavailable[key] is False for key in ("ready", "world_ready", "weather_known", "hdr")) and
            unavailable["typed_material_draws"] == 0 and unavailable["mixed_frame_sha256"] == unavailable["baseline_frame_sha256"] and
            unavailable["actual_submission"] is True and unavailable["native_capture"] is False,
            "Pending unknown authority invented content")
    two = data["two_selected"]
    require(two["selected_buildings"] == [2, 4] and len(two["outlines"]) == len(two["bodies"]) == len(two["points"]) == 2 and
            two["repeat_frame_exact"] is two["cache_handle_equal"] is True and two["added_vertices"] == 64 and two["added_indices"] == 96 and
            two["intervening_plain_commands"] > 0 and two["selected_frame_contract"] == by_id["textured-two-houses-selected"]["frame_contract"] and
            two["selected_frame_contract"]["vertex_count"] == two["clean_frame_contract"]["vertex_count"] + 64 and
            two["selected_frame_contract"]["index_count"] == two["clean_frame_contract"]["index_count"] + 96 and
            two["capture_rgba_sha256"] == hashlib.sha256(images["textured-two-houses-selected"].tobytes()).hexdigest(), "Two selected mapped houses are vacuous")
    for point, row, body, position in zip(two["points"], two["outlines"], two["bodies"], ([26., 22.], [32., 25.]), strict=True):
        ground = projected_ground(position, f32(1.3), 15.)
        close_values(point["ground"], ground)
        require(point["id"] == row["entity_id"] and row["texture"] == cache and body["material_index"] > 0, "Repeated entity material/outline identity differs")
        require(mask_at(sources[KEYS[0]], row["bounds"], point["point"])[2] >= 64, "Second selected point is not generated coverage")
        entry = sources[KEYS[0]]["entry"]
        sw, sh = entry["rect"][2:]
        width = f32(f32(f32(76. * f32(2.1)) * f32(1.3)) * f32(sw / f32(entry.get("reference_width", sw))))
        height = f32(width * f32(sh / sw))
        close_values(row["bounds"], [f32(ground[0] - f32(width * f32(entry["pivot"][0]))),
            f32(ground[1] - f32(height * f32(entry["pivot"][1]))), width, height])
        outline(row, row["bounds"], min(2.4, max(1.2, f32(f32(1.2) * f32(1.3)))))
    obstruction = two["selection_occlusion"]
    original_world = load(REPOSITORY / "assets/art/world.json")
    _, entry, _, paint = original_sprite(REPOSITORY / "assets", original_world, "building.workshop.ready")
    dims, values, coverage = pool(paint, *entry["rect"][2:])
    foreground = {"pool": (dims, values, coverage), "original_pool": (dims, values, coverage)}
    require(obstruction["foreground_id"] == 5 and obstruction["foreground_key"] == "building.workshop.ready" and
            obstruction["foreground_depth"] == 53 and obstruction["first_house_depth"] == 48 and
            obstruction["crop"] == entry["rect"] and obstruction["pivot"] == [f32(value) for value in entry["pivot"]] and
            obstruction["reference_width"] == f32(entry.get("reference_width", entry["rect"][2])) and
            obstruction["crop_rgba_sha256"] == hashlib.sha256(paint).hexdigest() and obstruction["mask_size"] == dims and
            obstruction["pooled_alpha_sha256"] == hashlib.sha256(values).hexdigest() and
            obstruction["qualification"] == "generated house pooled alpha>=64; competing front workshop pooled alpha<64" and
            two["points"][0]["selected_ids_after_click"] == [2] and two["points"][1]["selected_ids_after_click"] == [2,4], "Actual source-qualified additive selection differs")
    ground = projected_ground([29.,24.], f32(1.3), 15.)
    width = f32(f32(192. * f32(1.3)) * f32(entry["rect"][2] / f32(entry.get("reference_width", entry["rect"][2]))))
    height = f32(width * f32(entry["rect"][3] / entry["rect"][2]))
    close_values(obstruction["foreground_ground"], ground)
    close_values(obstruction["foreground_bounds"], [f32(ground[0] - f32(width * f32(entry["pivot"][0]))),
        f32(ground[1] - f32(height * f32(entry["pivot"][1]))), width, height])
    require(obstruction["requested_width"] == f32(192. * f32(1.3)) and obstruction["safe_point"] == two["points"][0]["point"] and
            obstruction["occluded_foreground_mask"] == mask_at(foreground, obstruction["foreground_bounds"], obstruction["occluded_point"]) and
            obstruction["occluded_foreground_mask"][2] >= 64 and
            obstruction["safe_foreground_mask"] == mask_at(foreground, obstruction["foreground_bounds"], obstruction["safe_point"]) and
            obstruction["safe_foreground_mask"][2] < 64 and
            obstruction["safe_house_mask"] == mask_at(sources[KEYS[0]], two["outlines"][0]["bounds"], obstruction["safe_point"]) and
            obstruction["safe_house_mask"][2] >= 64, "Source masks do not qualify the two-house unobscured click")
    require(two["outlines"][1]["command"] > two["bodies"][0]["command"], "Intervening body/outline order differs")
    ui = data["ui"]
    require(ui["hud"]["changed_pixels"] is None and ui["hud"]["actual_submission"] is True and ui["hud"]["native_capture"] is False and
            ui["hud"]["world_prefix_sha256"] == by_id["textured-body-default"]["frame_sha256"] and
            ui["interleave"]["menu_actual_submission"] is True and ui["interleave"]["menu_pixel_repeat_measured"] is False and
            ui["interleave"]["world_pixels_equal"] is None and ui["interleave"]["world_frame_equal"] is True and ui["interleave"]["world_actual_submission"] is True and
            ui["interleave"]["before_frame_sha256"] == ui["interleave"]["after_frame_sha256"] == by_id["textured-body-default"]["frame_sha256"], "Actual RmlUi/prefix/interleave differs")
    for name in ("menu_first", "hud"):
        require(ui[name]["ui_draws"] > 0 and ui[name]["textured_draws"] > 0 and ui[name]["distinct_textures"] >= 2 and
                ui[name]["premultiplied_draws"] > 0, "Actual RmlUi text/texture commands are vacuous")
    late = data["post_submit_rejection"]
    require(late["id"] == "after_actual_rml_menu" and "startup-only" in late["error"] and late["before_frame_sha256"] == late["after_frame_sha256"],
            "Post-submit catalog guard lost state")
    world = load(REPOSITORY / "assets/art/world.json")
    require([row["key"] for row in data["variants"]] == ["building.house.level2", "building.house.stage1", "building.house.stage2",
            "building.house.stage3", "building.house.winter"], "Exact fallback variant set differs")
    for row in data["variants"]:
        require(row["registered"] is False, "Fallback variant aliases a registered representative")
        verify_legacy_quad(REPOSITORY / "assets", world, row, f32(76. * f32(2.1)), f32(1.3), row["world"], 15.)
    animation = data["animation_cases"]
    require(len(animation) >= 4 and any(not row["mapped"] for row in animation) and any(row["facing"] != 2 for row in animation) and
            any(row["phase"] != 1 for row in animation), "Actual animation fallback is vacuous")
    for row in animation:
        require(row["key"] == f"resident.man.walk.{row['facing']}.{row['phase']}" and row["mapped"] is (row["key"] == KEYS[1]) and
                row["moving"] is True and 1 <= row["step"] <= 120 and row["authority_tick"] == 7200 + row["step"] and
                7200 <= row["shown_tick"] <= row["authority_tick"], "Exact animation representative was frozen")
        if not row["mapped"]:
            legacy_row = dict(row, requested_width=f32(39. * f32(1.3)))
            verify_legacy_quad(REPOSITORY / "assets", world, legacy_row, 39., f32(1.3), row["displayed_position"], 6.)
    verify_direct(data, sources)
    return {"visible_world": visible, "captures": 13, "typed_zoom_queries": 6, "outline_offsets": 8,
            "two_selected_entities": 2, "direct_api_controls": 15, "invalid_cache_limits": 2}


def verify_body_visibility(row: dict, source: dict, query_row: dict, images: dict, captures: dict, changed) -> None:
    require(row["id"] == "same_frame_house_body_removal" and row["source_capture"] == "textured-body-default.png" and
            row["removed_capture"] == "textured-body-default-house-removed.png" and row["only_house_command_removed"] is row["carrier_payload_exact"] is True,
            "Paired body identity/scope differs")
    original, removed = row["source_frame"], row["removed_frame"]
    expected = copy.deepcopy(original)
    index = row["removed_command_index"]
    require(type(index) is int and 0 <= index < len(expected["commands"]) and expected["commands"][index] == {"geometry": row["removed_command"]},
            "Body removal targeted another command")
    del expected["commands"][index]
    require(removed == expected and len(original["sprites"]) == len(removed["sprites"]) == 2 and len(original["surfaces"]) == len(original["vertices"]),
            "Paired body removal changed vertex/material/lighting/companion payload")
    require(row["source_frame_sha256"] == captures["textured-body-default"]["frame_sha256"] and
            row["removed_frame_sha256"] == captures["textured-body-default-house-removed"]["frame_sha256"] and
            row["source_frame_sha256"] != row["removed_frame_sha256"], "Paired full serializers do not tie to actual captures")
    # Equality uses every actual serializer field. SHA links producer dump bytes
    # to captures; Python and nlohmann float text conventions are not equated.
    require(row["body"] == query_row["mixed"] and row["removed_command_index"] == query_row["mixed"]["command"] and
            row["removed_command"] == row["body"]["draw"] and row["removed_command"][5] == 6 and
            row["removed_first_index"] == row["body"]["first_index"] == row["removed_command"][4], "Paired body quad differs from source query")
    at = row["removed_first_index"]
    indices = original["indices"][at:at + 6]
    require(row["removed_indices"] == indices and len(indices) == 6 and len(row["removed_vertex_indices"]) == 4 and
            len(set(row["removed_vertex_indices"])) == 4 and set(indices) == set(row["removed_vertex_indices"]) and
            [original["vertices"][index] for index in row["removed_vertex_indices"]] == row["body"]["vertices"], "Paired actual indices/vertices differ")
    close_values(row["bounds"], query_row["bounds"], .000001)
    require(row["generated_rgba_sha256"] == hashlib.sha256(source["rgba"]).hexdigest() and row["qualification"] ==
            "pixel-center local UV; binary64 floor to generated source pixel; all 3x3 alpha samples255 inside bound", "Paired generated coverage qualification differs")
    x, y, w, h = row["bounds"]
    sw, sh = source["entry"]["rect"][2:]
    alpha = source["rgba"][3::4]
    before = images["textured-body-default"].tobytes()
    after = images["textured-body-default-house-removed"].tobytes()
    qualified = different = 0
    for py in range(max(0, math.ceil(y - .5)), min(1080, math.ceil(y + h - .5))):
        for px in range(max(0, math.ceil(x - .5)), min(1920, math.ceil(x + w - .5))):
            sx, sy = math.floor(((px + .5) - x) / w * sw), math.floor(((py + .5) - y) / h * sh)
            if not (1 <= sx < sw - 1 and 1 <= sy < sh - 1):
                continue
            if not all(alpha[(sy + dy) * sw + sx + dx] == 255 for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                continue
            qualified += 1
            position = (py * 1920 + px) * 4
            different += before[position:position + 4] != after[position:position + 4]
    require(row["qualified_pixels"] == qualified > 0 and row["changed_pixels"] == different > 0 and
            row["total_changed_pixels"] == changed("textured-body-default", "textured-body-default-house-removed") and
            row["changed_pixels_in_bounds"] == changed("textured-body-default", "textured-body-default-house-removed", row["bounds"]) >= different,
            "Actual same-frame generated body contribution is vacuous or misreported")


def verify_painted_default(painted: Path, row: dict) -> None:
    require(row["id"] == "current_exporter_absent_mode_default" and row["read_profile"] == 0 and row["copied_profile_exact"] is True and
            row["registered_count"] == 2 and row["source_record_fields"] == 3 and row["submitted_after_all_cold_rejections"] is True and
            row["captures"] == 0 and row["primary_frame_camera_pick_registry_exact"] is True and
            row["primary_before_frame_sha256"] == row["primary_after_frame_sha256"] and row["primary_selected"] == [2] and
            row["scope"] == "current-exporter absent-mode Default cold install, exact original RGBA/pooled picking and actual normal LegacyCombined submission; no PNG or neutral claim",
            "Current Default compatibility control differs")
    sources = collection_sources(painted, {"registrations": row["registrations"]}, neutral_mode=False)
    art = load(painted / "art/world.json")
    expected_channels = []
    for texture in art["textures"]:
        raw = (painted / texture["path"]).read_bytes()
        expected_channels.append({"id": texture["id"], "path": texture["path"], "bytes": len(raw),
                                  "sha256": hashlib.sha256(raw).hexdigest(), "prefix": list(raw[:16])})
    require(row["raw_channels"] == expected_channels and len(expected_channels) == 12, "Current Default raw channels differ")
    for name, key in (("house", KEYS[0]), ("resident", KEYS[1])):
        query(sources[key], row[name], f32(1.3))
        require(row[name + "_rgba_sha256"] == hashlib.sha256(sources[key]["paint"]).hexdigest() and
                row[name + "_pool_sha256"] == hashlib.sha256(sources[key]["original_pool"][2]).hexdigest(), "Default alpha/pool identity differs")
    frame_contract({"id": "painted-default", "frame_contract": row["frame_contract"], "telemetry": row["telemetry"]}, captured=False)
    for name in ("house", "resident"):
        require(any(role["command"] == row[name]["mixed"]["command"] and role["draw"] == row[name]["mixed"]["draw"]
                    for role in row["frame_contract"]["typed_roles"]), "Default queried body is not the actually submitted frame")
    close_values(row["primary_camera"], projected_ground([24., 24.], f32(1.3), 0.))


def verify_common(data: dict, build: Path, ui: Path, material: Path, neutral: Path, painted: Path, cpu: Path, profile: str) -> dict[str, dict]:
    expected_saves = list(SAVES if profile == "positive" else SAVES[:1])
    require(data["world_saves"] == expected_saves and [row["file"] for row in data["fixtures"]] == expected_saves,
            "Actual save fixture inventory differs")
    for index, row in enumerate(data["fixtures"]):
        tick = 21600 if index == 1 else 7200
        calendar = tick + (30 * 28800 if index == 4 else 0)
        require(row["tick"] == tick and row["calendar_tick"] == calendar and row["season"] == (calendar // (10 * 28800)) % 4 and
                row["authored_fixture"] is True and row["earned_history"] is False and type(row["checksum"]) is int and
                0 < row["checksum"] < 2**64, "Authored fixture misrepresents earned weather history")
    require(len({row["checksum"] for row in data["fixtures"]}) == len(expected_saves), "Distinct authored save checksums are vacuous")
    hardware = data["hardware"]
    require(set(hardware) == {"backend", "gpu", "driver", "software", "graph", "resolution", "fps", "gpu_execution_ms"} and
            hardware["backend"] == "direct3d12" and hardware["gpu"] and hardware["driver"] and hardware["software"] is False and
            hardware["graph"] == "LegacyCombined" and hardware["resolution"] == [1920, 1080] and
            hardware["fps"] is hardware["gpu_execution_ms"] is None, "Actual hardware/route/performance scope differs")
    original = REPOSITORY / "assets"
    world = load(original / "art/world.json")
    require(data["inputs"] == {"original_pages": 67, "original_records": 987,
            "original_content_hash": manifest_content(load(original / "manifest.json")),
            "collection_content_hash": manifest_content(load(material / "manifest.json")),
            "neutral_content_hash": manifest_content(load(neutral / "manifest.json")),
            "painted_content_hash": manifest_content(load(painted / "manifest.json")),
            "read_profiles": {"original": 0, "textured": 2, "neutral": 1, "painted": 0, "ui": 0},
            "ui_content_hash": manifest_content(load(ui / "manifest.json")),
            "original_world_sha256": digest(original / "art/world.json"),
            "collection_metadata_sha256": digest(material / "art/material-collection.json"),
            "collection_world_sha256": digest(material / "art/world.json")} and world["schema"] == 1 and
            len(world["pages"]) == 67 and sum(len(page["sprites"]) for page in world["pages"]) == 987, "Input identities differ")
    require(not any("accepted" == item.lower() for item in data["open_delivery"]), "Native scope invents acceptance")
    if profile != "positive":
        require(data["scope"] == "Cold MaterialCollectionTextured shard actual constructor/read/View rejections before submission, then current all-painted Default compatibility normal submission without PNG; no physical allocation-fault claim" and
                data["open_delivery"] == ["Positive GPU proof in separate profile", "Physical GPU OOM and bad_alloc", "Production/reference acceptance",
                "Full E01-E17/P01", "Normal main startup", "FPS/GPU timing"], "Cold scope differs")
    else:
        require(data["scope"] == "Actual normal GameView::compose LegacyCombined three-texture XYZ House plus unchanged painted resident, old MaterialCollection and all-painted Default captures; UV algorithm bound separately to accepted CPU proof; art/reference/whole-engine acceptance OPEN" and
                all(claim in data["open_delivery"] for claim in ("Full E01-E17/P01 engine acceptance", "full normal main startup and package/release",
                "production neutral albedo and all eight authored material masters", "reference fidelity", "FPS and GPU execution timing",
                "physical GPU OOM/bad_alloc/plain-outline injection", "normal GameView mirrored fallback untriggered with canonical full987 catalog")),
                "Positive diagnostic overstates scope")
    prerequisite = data["cpu_prerequisite"]
    proof = load(cpu)
    require(prerequisite == {"report_path": cpu.as_posix(), "report_bytes": cpu.stat().st_size, "report_sha256": digest(cpu),
        "producer": proof["producer"], "test_source": proof["test_source"], "report_preserved": True,
        "scope": "bound complete independent CPU algorithm proof; native RGB membership is not exact UV acceptance"}, "Native CPU prerequisite binding differs")
    sources = collection_sources(material, data)
    verify_source_comparison(material, neutral, data, sources)
    return sources


def semantic_negatives(native: dict, sources: dict[str, dict]) -> int:
    """Focused rejection controls operate on copies; no retained bytes change."""
    checks = 0
    if native["profile"] == "positive":
        original = native["deterministic"]["captures"][1]
        for key, value in (("sprite_material_count", 0), ("non_neutral_companion_count", 1), ("hdr", False)):
            changed = copy.deepcopy(original)
            changed["frame_contract"][key] = value
            try:
                frame_contract(changed)
            except AssertionError:
                checks += 1
            else:
                raise AssertionError("Malformed frame oracle was accepted")
        changed = copy.deepcopy(native["deterministic"]["zoom_cases"][0]["house"])
        changed["crop_rgba_sha256"] = hashlib.sha256(sources[KEYS[0]]["paint"]).hexdigest()
        try:
            query(sources[KEYS[0]], changed, f32(1.3))
        except AssertionError:
            checks += 1
        else:
            raise AssertionError("Old painted alpha substituted for generated source")
    return checks


def repeat_semantics(native: dict) -> dict:
    """Ignore only measured duration and the named, independently bound save envelopes."""
    comparable = copy.deepcopy(native)
    del comparable["measured"]
    names = set(SAVES if native["profile"] == "positive" else SAVES[:1])
    seen = set()
    for row in comparable["deterministic"]["output_inventory_excluding_report"]:
        if row["path"] not in names:
            continue
        require(set(row) == {"path", "bytes", "sha256"} and row["path"] not in seen,
                "Named save envelope inventory shape differs")
        seen.add(row["path"])
        # verify_native already bound each actual size/SHA to the file and the
        # wrapper. World fixture checksums and preservation remain compared.
        # Only these save envelopes contain independent random UUID tokens.
        row["bytes"] = None
        row["sha256"] = None
    require(seen == names, "Named save envelope inventory is incomplete")
    return comparable


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    names = ("build", "objects", "kit", "ui", "material", "neutral", "painted", "cpu", "positive_first", "positive_second",
             "cold_source_first", "cold_source_second", "cold_limits_first", "cold_limits_second")
    for name in names:
        parser.add_argument(name)
    args = parser.parse_args()
    require(os.name == "nt", "Local Windows evidence required")
    paths = {}
    for name in names:
        text = getattr(args, name)
        require(all(part not in {"", ".", ".."} for part in text.replace("\\", "/").split("/")[1:]), "Raw evidence path contains alias components")
        raw = Path(text)
        require(raw.is_absolute() and all(part not in {".", ".."} for part in raw.parts), "Raw evidence path must be absolute without dot traversal")
        no_reparse(raw)
        path = raw.resolve(strict=True)
        require(path.is_relative_to(REPOSITORY / "build") and (path.is_file() if name == "cpu" else path.is_dir()), "Evidence path escaped build or has wrong role")
        if name not in {"kit", "ui", "material", "neutral", "painted", "cpu"}:
            require(path.parent == REPOSITORY / "build", "Artifact root must be an immediate build child")
        paths[name] = path
    require(len(set(paths.values())) == len(paths), "Distinct evidence roles/roots required")
    build, objects, kit, ui, material, neutral, painted, cpu = (paths[name] for name in names[:8])
    output_names = names[8:]
    root_rows = {}
    for name, root in paths.items():
        if name == "cpu": continue
        aliases = cold_aliases(root, material) if name.startswith("cold_limits_") else {}
        root_rows[name] = inventory(root, aliases)
    preferences = Path(os.environ["APPDATA"]) / "Ribterra/Ribterra"
    require(preferences.is_dir(), "Ordinary preference root must already exist")
    preference_rows = inventory(preferences)
    require(len(preference_rows) == 36, "Ordinary preference baseline must contain exactly36 unique files")
    own_reader = Path(__file__).resolve(strict=True)
    initial = {Path(row["path"]): (row["bytes"], row["sha256"]) for rows in root_rows.values() for row in rows}
    initial[cpu] = (cpu.stat().st_size, digest(cpu).upper())
    initial[own_reader] = (own_reader.stat().st_size, digest(own_reader).upper())
    initial.update({Path(row["path"]): (row["bytes"], row["sha256"]) for row in preference_rows})
    report, checked = verify_build(build, objects, kit, ui, material, neutral, painted, cpu)
    frozen_reader = next(row for row in report["source_provenance"] if row["relative_path"] == "tests/gameview_textured_material_evidence_tests.py")
    require(Path(frozen_reader["repository_path"]) == own_reader == REPOSITORY / "tests/gameview_textured_material_evidence_tests.py" and
            Path(frozen_reader["snapshot_path"]) == build / "source/tests/gameview_textured_material_evidence_tests.py" and
            digest(own_reader).upper() == digest(Path(frozen_reader["snapshot_path"])).upper() == frozen_reader["sha256"].upper(),
            "Current reader does not match its exact compiled source snapshot")
    checked.add(own_reader)
    build_required = frozenset(checked)
    build_guards = {path: (path.stat().st_size, digest(path).upper()) for path in build_required}
    witnesses, negative_count, total_bytes = {}, 0, sum(total(rows) for name, rows in root_rows.items() if name in {"build", "objects"})
    for profile, pair in (("positive", ("positive_first", "positive_second")), ("cold-source", ("cold_source_first", "cold_source_second")),
                          ("cold-limits", ("cold_limits_first", "cold_limits_second"))):
        natives, derived = [], []
        for name in pair:
            output = paths[name]
            current_checked = set(build_required)
            native = verify_native(build, objects, kit, ui, material, neutral, painted, cpu, profile, output, report, current_checked)
            sources = verify_common(native["deterministic"], build, ui, material, neutral, painted, cpu, profile)
            if profile == "positive":
                derived.append(verify_positive(output, native["deterministic"], sources))
            else:
                verify_painted_default(painted, native["deterministic"]["painted_default_compatibility"])
                derived.append({"cold_controls": len(cold_ids(profile)), "variant_catalogs": 12, "captures": 0})
            negative_count += semantic_negatives(native, sources)
            natives.append(native)
            total_bytes += total(root_rows[name])
        require(repeat_semantics(natives[0]) == repeat_semantics(natives[1]) and derived[0] == derived[1], "Repeated profile semantics differ")
        for row in natives[0]["deterministic"]["output_inventory_excluding_report"]:
            if row["path"] not in SAVES:
                require((paths[pair[0]] / row["path"]).read_bytes() == (paths[pair[1]] / row["path"]).read_bytes(), "Repeated non-save payload differs")
        witnesses[profile] = derived[0]
    require(set(COLD_CASES["cold-source"]).isdisjoint(COLD_CASES["cold-limits"]) and
            len(set(COLD_CASES["cold-source"]) | set(COLD_CASES["cold-limits"])) == 24 and
            sum(witnesses[name]["cold_controls"] for name in ("cold-source", "cold-limits")) == 29,
            "Both bounded cold shards and exact union are mandatory")
    for path, identity in {**initial, **build_guards}.items():
        no_reparse(path)
        require((path.stat().st_size, digest(path).upper()) == identity, "Read-only reader observed changed evidence: " + str(path))
    for name, rows in root_rows.items():
        aliases = cold_aliases(paths[name], material) if name.startswith("cold_limits_") else {}
        require(indexed_inventory(inventory(paths[name], aliases)) == indexed_inventory(rows), "Evidence root membership changed during review")
    require(indexed_inventory(inventory(preferences)) == indexed_inventory(preference_rows), "Ordinary preference membership/bytes changed")
    print(f"Textured material six-profile-repeat evidence passed; sources={len(report['source_provenance'])}; zero_commands=122; object_files=24; "
          f"individual_root_limit={MAXIMUM}; honest_eight_artifact_root_aggregate_bytes={total_bytes}; semantic_negative_controls={negative_count}; witnesses={witnesses}; "
          "UV=separately_bound_accepted_independent_CPU; normal_main=false; references=false; FPS=null; full_engine=false")


if __name__ == "__main__":
    main()
