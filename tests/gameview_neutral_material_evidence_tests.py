#!/usr/bin/env python3
"""Read-only proof reader for separate positive/cold provisional neutral cohorts.

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
from gameview_neutral_material_export_tests import oracle, check_channels, check_palette, report_check
TRANSLATION_UNITS = (UI_TRANSLATION_UNITS - {"tools/gameview_ui_probe.cpp"}) | {"tools/gameview_neutral_material_probe.cpp"}
MAXIMUM = 128 * 1024**2
MINIMUM_FREE = 384 * 1024**2
KEYS = ("building.house.ready", "resident.man.walk.2.1")
CAPTURES = ("neutral-menu-first", "neutral-body-default", "neutral-body-default-house-removed", "neutral-original-default", "neutral-outline-default",
    "neutral-body-min", "neutral-outline-min", "neutral-body-max", "neutral-outline-max", "neutral-mixed-night",
    "neutral-mixed-wet", "neutral-world-hud", "neutral-two-houses-selected", "neutral-world-restored",
    "neutral-variants", "neutral-winter", "neutral-animation-fallback", "neutral-mine-fallback",
    "neutral-mine-legacy", "neutral-authority-unavailable")
SAVES = tuple(f"neutral-material-{name}.save" for name in ("day", "night", "wet", "variants", "winter", "two-houses"))
API_IDS = ("unknown-id-alpha-zero", "invalid-pass-alpha-zero", "invalid-content-alpha-zero", "radius-negative",
    "radius-nan", "radius-infinite", "zoom-zero", "zoom-nan", "scale-zero", "ground-nan", "prefix-short",
    "prefix-long", "authored-prefix-not-synthesized", "alpha-zero", "radius-zero")
COLD_CASES = ("schema", "reference_width", "pivot", "decoded_crop_hash", "legacy_key", "albedo_byte",
    "neutral-null", "neutral-extra", "palette-scalar", "authored-provenance", "coverage", "generated-sha",
    "palette-declared-oversize", "palette-actual-oversize", "palette-reread-oversize", "notice-reparse",
    "root-reparse", "palette-internal-reparse", "palette-schema", "palette-renamed-id")
COLD_IDS = ("default-profile-authored-mode", "unknown-read-profile") + tuple(
    case + "-" + operation if case == "palette-reread-oversize" else case
    for case in COLD_CASES for operation in (("verify", "read", "read_path") if case == "palette-reread-oversize" else ("",)))
COLD_DIAGNOSTICS = {
    "default-profile-authored-mode": "MaterialCollection read profile", "unknown-read-profile": "Unknown asset catalog read profile",
    "schema": "Normal material collection schema/counts", "reference_width": "Registered source crop/pivot/reference width/catalog hash",
    "pivot": "Registered source crop/pivot/reference width/catalog hash", "decoded_crop_hash": "Registered decoded paint hash",
    "legacy_key": "unique exact supported original legacy key", "albedo_byte": "Registered albedo must preserve the exact original cropped RGBA",
    "neutral-null": "Neutral material object has missing or extra fields", "neutral-extra": "Neutral material object has missing or extra fields",
    "palette-scalar": "Neutral material scalar", "authored-provenance": "palette manifest/provenance binding", "coverage": "coverage declarations differ",
    "generated-sha": "Generated neutral albedo digest", "palette-declared-oversize": "Material palette declared bytes exceed 512 KiB",
    "palette-actual-oversize": "Material palette exceeds 512 KiB", "palette-reread-oversize": "Material palette exceeds 512 KiB",
    "notice-reparse": "Neutral material read rejects absent/reparse", "root-reparse": "Material catalog rejects",
    "palette-internal-reparse": "Material catalog rejects", "palette-schema": "Neutral material integer is outside its bounded range"}


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

def verify_build(build: Path, objects: Path, kit: Path, ui_kit: Path, material: Path, painted: Path) -> tuple[dict, set[Path]]:
    report = load(build / "build-report.json")
    repository = Path(report["authoritative_repository"])
    require(repository == Path(r"C:\Users\fipst\Desktop\ribterra"), "Authoritative repository differs")
    require(Path(report["output_root"]) == build and Path(report["control_kit"]["root"]) == kit,
            "Build/kit binding mismatch")
    require(report["build_label"] == "gameview-neutral-material-isolated-unversioned" and
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
            len(report["material_collection"]["files"]) == 20 and
            report["original_art"]["bulk_staged"] is False and
            report["material_collection"]["bulk_staged"] is False and
            report["material_collection"]["production_accepted"] is False,
            "Normal composition original/material closure or scope differs")
    for section, root, count in (("original_art", repository / "assets", 181), ("material_collection", material, 20)):
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
        "tools/gameview_neutral_material_probe.cpp", "tools/engine_control_probe.hpp",
        "licenses/Cormorant-OFL.txt", "licenses/Noto-OFL.txt", "licenses/PTSerif-OFL.txt", "licenses/Ribterra-Assets.txt",
        "tools/export_engine_ui_kit.py", "tools/asset_tool.py", "src/ui/game_ui.cpp",
        "include/ribterra/render_frame.hpp", "src/render/renderer2d.cpp",
        *{f"src/ui/game_ui_{name}.inc" for name in ("lobby", "manual", "settlement", "economy", "buildings", "professions")},
        "tools/build_gameview_neutral_material_isolated.ps1", "tools/run_gameview_neutral_material_isolated.ps1",
        "tools/export_engine_control_kit.py", "tools/export_material_master.py",
        "tests/gameview_neutral_material_evidence_tests.py", "tests/gameview_cumulative_evidence_tests.py",
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
                      '/DRIBTERRA_PROBE_BUILD="gameview-neutral-material-isolated-unversioned"',
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
        unit_flags = (["/Zc:inline"] if source.name == "gameview_neutral_material_probe.cpp" else
                      ["/DSTEAMNETWORKINGSOCKETS_STATIC_LINK"] if source.name == "gns_transport.cpp" else [])
        require(argv == expected_flags + unit_flags + ["/c", str(source),
                "/Fo" + str(objects / (source.stem + ".obj"))],
                "Actual compile arguments contain unbound inputs or overriding options")
        require(argv.count("/Zc:inline") == (1 if source.name == "gameview_neutral_material_probe.cpp" else 0) and
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
         "/out:" + str(build / "GameViewNeutralMaterialProbe.exe")] + [row["path"] for row in report["objects"]] +
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
    exe = build / "GameViewNeutralMaterialProbe.exe"
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
    for input_root, section, count in ((material, "material_source_inputs", 20), (painted, "painted_source_inputs", 14)):
        verify_producer_inputs(input_root, section, count, repository, report, checked)
    return report, checked


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


def collection_sources(material: Path, data: dict, neutral_mode: bool = True) -> dict[str, dict]:
    original = REPOSITORY / "assets"
    world = load(original / "art/world.json")
    art = load(material / "art/world.json")
    metadata = load(material / "art/material-collection.json")
    files = {row["relative_path"]: Path(row["path"]).read_bytes() for row in inventory(material)}
    cpu_report = report_check(REPOSITORY, material, files)
    descriptor_path = next(row["source"] for row in load(material / "manifest.json")["assets"] if row["id"] == "art.world")
    descriptor = load(REPOSITORY / descriptor_path)
    by_key = {item["legacy_key"]: item for item in descriptor["items"]}
    require(set(by_key) == set(KEYS) and len(metadata["items"]) == len(art["lit_sprites"]) ==
            len(art["materials"]) == 2 and len(art["textures"]) == 12 and art["schema"] == 2 and art["pages"] == [],
            "Mixed neutral/painted descriptor subset differs")
    require(metadata["paint_colour"] == ("authored-XYZ-palette-draft-and-legacy-painted-neutral-acceptance-pending" if neutral_mode else "legacy-painted-neutral-albedo-acceptance-pending") and
            metadata["reference_fidelity"] == "NOT_ACCEPTED" and metadata["production_master_acceptance"] == "OPEN" and
            metadata["normal_startup"] == metadata["full_engine"] == "OPEN", "Provisional source claims differ")
    require([row["legacy_key"] for row in data["registrations"]] == list(KEYS), "Registry witness order differs")
    result = {}
    textures = {texture["id"]: texture for texture in art["textures"]}
    materials = {entry["id"]: entry for entry in art["materials"]}
    sprites = {entry["id"]: entry for entry in art["lit_sprites"]}
    manifest = load(material / "manifest.json")
    manifest_content(manifest)
    for item, actual in zip(metadata["items"], data["registrations"], strict=True):
        key = item["legacy_key"]
        page, entry, page_size, paint = original_sprite(original, world, key)
        record = source_record(item)
        registration = item["registration"]
        expected_catalog = source_hash({"page": {"path": page["path"]}, "sprite": entry})
        expected_registration = {"legacy_key": key, "sprite_id": item["sprite_id"], "material_id": item["material_id"],
                           "source_hash": source_hash(record), "source_record_sha256": source_hash(record),
                           "source_record": record, "catalog_entry_sha256": expected_catalog}
        if not neutral_mode:
            expected_registration = {field: expected_registration[field] for field in
                                     ("legacy_key", "sprite_id", "material_id", "source_record", "source_record_sha256")}
        require(actual == expected_registration and
                item["source_hash"] == source_hash(record) == materials[item["material_id"]]["source_hash"],
                "Flattened logical source identity differs")
        require(registration["page_path"] == page["path"] and registration["rect"] == entry["rect"] and
                registration["pivot"] == entry["pivot"] and registration["source_size"] == entry["rect"][2:] and
                registration["reference_width"] == f32(entry.get("reference_width", entry["rect"][2])) and
                registration["png_sha256"] == digest(original / page["path"]) and
                registration["decoded_crop_sha256"] == hashlib.sha256(paint).hexdigest() and
                registration["catalog_entry_sha256"] == expected_catalog, "Original registration evidence differs")
        close_values(registration["pivot_px"], [f32(f32(v) * extent) for v, extent in zip(entry["pivot"], entry["rect"][2:])], .000001)
        source = by_key[key]
        master_path = REPOSITORY / source["master_path"]
        require(digest(master_path) == item["master_sha256"] == source["master_sha256"], "Active authored geometry digest differs")
        master = load(master_path)
        channels = {}
        for channel, slot in materials[item["material_id"]]["channels"].items():
            texture = textures[slot["texture"]]
            require(slot["rect"] == [0, 0, *registration["source_size"]] and
                    [texture["width"], texture["height"]] == registration["source_size"] and
                    texture["sha256"] == digest(material / texture["path"]), "Generated channel binding differs")
            channels[channel] = files[texture["path"]]
        neutral = "albedo_source" in item
        require(neutral is (neutral_mode and key == KEYS[0]), "Expected explicit/absent albedo mode differs")
        expected = oracle(master, registration, None if neutral else paint)
        check_channels(channels, expected)
        if neutral:
            albedo_source = item["albedo_source"]
            require(albedo_source["kind"] == "authored-xyz-triangle-materials-v1" and
                    digest(material / albedo_source["palette_path"]) == albedo_source["palette_sha256"] and
                    digest(material / albedo_source["provenance"]["notice_path"]) == albedo_source["provenance"]["notice_sha256"] and
                    item["generated_albedo_sha256"] == hashlib.sha256(channels["albedo"]).hexdigest(), "Packed palette/notice/albedo identity differs")
            check_palette(REPOSITORY, source, master, load(material / albedo_source["palette_path"]))
            old, new = paint[3::4], channels["albedo"][3::4]
            counts = {"original_alpha_samples": sum(value != 0 for value in old),
                      "generated_alpha_samples": sum(value != 0 for value in new),
                      "overlap_alpha_samples": sum(a != 0 and b != 0 for a, b in zip(old, new)),
                      "omitted_original_alpha_samples": sum(a != 0 and b == 0 for a, b in zip(old, new)),
                      "generated_only_alpha_samples": sum(a == 0 and b != 0 for a, b in zip(old, new))}
            require(item["coverage"] == counts and set(new) == {0, 255} and counts["omitted_original_alpha_samples"] > 0 and
                    item["supported_alpha_samples"] == counts["generated_alpha_samples"] and
                    item["uncovered_alpha_samples"] == counts["omitted_original_alpha_samples"], "Generated geometry coverage differs")
        else:
            require(channels["albedo"] == paint, "Absent-mode painted bytes changed")
        rgba = channels["albedo"]
        result[key] = {"page": page, "entry": entry, "page_size": page_size, "paint": paint, "rgba": rgba,
                       "pool": pool(rgba, *registration["source_size"]), "original_pool": pool(paint, *registration["source_size"]),
                       "item": item, "lit": sprites[item["sprite_id"]]}
    house = result[KEYS[0]]
    if neutral_mode:
        require(data["house_source"] == {"legacy_key": KEYS[0], "original_rgba_sha256": hashlib.sha256(house["paint"]).hexdigest(),
            "generated_rgba_sha256": hashlib.sha256(house["rgba"]).hexdigest(), "coverage": house["item"]["coverage"],
            "original_pool_sha256": hashlib.sha256(house["original_pool"][2]).hexdigest(),
            "generated_pool_sha256": hashlib.sha256(house["pool"][2]).hexdigest()}, "Neutral silhouette source witness differs")
    return result


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
            close_values(surface["anchor_export"], [*anchor, 4.], .000001)
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
    removed = capture["id"] == "neutral-body-default-house-removed"
    require(frame["typed_command_count"] == len(frame["typed_roles"]) == frame["sprite_material_count"] - int(removed) and
            frame["non_neutral_companion_count"] == 0 and frame["neutral_companion_count"] == frame["companion_count"] and
            frame["companion_count"] == (frame["vertex_count"] if frame["hdr"] else 0), "Companion stream differs")
    expected = 3 if capture["id"] == "neutral-two-houses-selected" else 1 if capture["id"] in {"neutral-variants", "neutral-winter"} else 2
    zero = {"neutral-menu-first", "neutral-original-default", "neutral-mine-fallback", "neutral-mine-legacy", "neutral-authority-unavailable"}
    if capture["id"] in zero:
        expected = 0
    require(frame["sprite_material_count"] == expected or capture["id"] == "neutral-animation-fallback" and
            frame["sprite_material_count"] in (1, 2), "Exact material subset differs")
    require(frame["hdr"] is (frame["sprite_material_count"] > 0), "Mapped HDR route differs")
    neutral = [[[0., 0., 0.], [0., 0., 1.]]] * frame["companion_count"]
    require(frame["companion_sha256"] == hashlib.sha256(canonical(neutral)).hexdigest(), "Whole neutral companion digest differs")
    require(frame["content"] == (1 if capture["id"] == "neutral-menu-first" else 0), "FrameContent differs")
    if capture["id"] == "neutral-authority-unavailable":
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


def verify_native(build: Path, objects: Path, kit: Path, ui: Path, material: Path, painted: Path, profile: str,
                  output: Path, report: dict, checked: set[Path]) -> dict:
    execution = load(output / "native-execution.json")
    require(execution["schema"] == 1 and execution["profile"] == profile and execution["accepted"] is True and
            execution["actual_subprocess"] is True and execution["signed_evidence"] is False and execution["exit_code"] == 0 and
            execution["native_created_output"] is True and execution["bound_inputs_unchanged"] is True and execution["integrity_errors"] == [], "Actual native wrapper rejected")
    roots = {"repository": REPOSITORY, "build_root": build, "object_root": objects, "kit_root": kit,
             "ui_kit_root": ui, "original_art_root": REPOSITORY / "assets", "material_collection_root": material,
             "painted_compatibility_root": painted, "output_root": output}
    require(all(Path(execution[key]) == value for key, value in roots.items()), "Native root linkage differs")
    exe = build / "GameViewNeutralMaterialProbe.exe"
    argv = [str(exe), profile, str(build / "assets"), str(REPOSITORY / "assets"), str(material), str(painted), str(ui), str(output)]
    require(execution["actual_argv"] == argv and execution["arguments"] == argv[1:] and Path(execution["executable"]) == exe,
            "Actual profile/argv differs")
    for field, path in (("runner", REPOSITORY / "tools/run_gameview_neutral_material_isolated.ps1"),
                        ("build_report", build / "build-report.json"), ("artifact_storage", build / "artifact-storage.json"),
                        ("input_final_binding", output / "input-final-hashes.json"),
                        ("combined_output_binding", output / "execution-output.txt")):
        require(Path(execution[field]["path"]) == path, "Selected wrapper binding differs")
        hash_row(execution[field], path)
    source_by_name = {row["relative_path"]: row for row in report["source_provenance"]}
    runner = source_by_name["tools/run_gameview_neutral_material_isolated.ps1"]
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
                      ("original", REPOSITORY / "assets"), ("material", material), ("painted", painted)):
        require(indexed_inventory(execution["protected_trees"][key]) == indexed_inventory(inventory(root)), "Native tree membership changed")
    preferences = execution["ordinary_preferences"]
    expected_preferences = Path(os.environ["APPDATA"]) / "Ribterra/Ribterra"
    require(Path(preferences["root"]) == expected_preferences and preferences["existed_before"] is preferences["existed_after"] is expected_preferences.is_dir() and
            indexed_inventory(preferences["files_before"]) == indexed_inventory(preferences["files_after"]) ==
            indexed_inventory(inventory(expected_preferences) if expected_preferences.is_dir() else []),
            "Ordinary preferences changed")
    storage = execution["storage"]
    require(storage == {"maximum_bytes_each": MAXIMUM, "minimum_free_bytes": MINIMUM_FREE, "aggregate_limit_claimed": False,
            "build_bytes": total(inventory(build)), "object_bytes": total(inventory(objects)),
            "build_plus_object_bytes": total(inventory(build)) + total(inventory(objects))}, "Wrapper storage sum differs")
    require(execution["claims"] == {"fps": None, "gpu_execution_ms": None, "production_reference": False,
            "all_eight_masters": False, "normal_main": False, "full_engine": False}, "Wrapper overstates delivery")
    aliases = cold_aliases(output, material) if profile == "cold" else {}
    actual = inventory(output, aliases)
    recorded = execution["native_output_before_runner_evidence"]
    extras = {"native-execution.json", "execution-output.txt", "input-final-hashes.json"}
    native_rows = [row for row in actual if row["relative_path"] not in extras]
    require(set(recorded) == {"file_count", "total_bytes", "files"} and recorded["file_count"] == len(native_rows) and
            recorded["total_bytes"] == total(native_rows) and indexed_inventory(recorded["files"]) == indexed_inventory(native_rows) and
            total(actual) <= MAXIMUM, "Exact retained profile inventory/budget differs")
    native = load(output / "neutral-material-report.json")
    require(set(native) == {"schema", "profile", "complete", "deterministic", "measured"} and native["schema"] == 1 and
            native["profile"] == profile and native["complete"] is True and set(native["measured"]) == {"total_ms"} and
            type(native["measured"]["total_ms"]) in (int, float) and math.isfinite(native["measured"]["total_ms"]) and native["measured"]["total_ms"] >= 0,
            "Actual profile report incomplete or timing scope differs")
    data = native["deterministic"]
    require(data["output_inventory_excluding_report"] == lower_rows([row for row in native_rows if row["relative_path"] != "neutral-material-report.json"]),
            "Probe native inventory differs")
    trees = {"runtime": build / "assets", "original": REPOSITORY / "assets", "collection": material, "painted": painted, "ui": ui}
    expected_protected = {key: lower_rows(inventory(root)) for key, root in trees.items()}
    require(data["protected_before"] == data["protected_after"] == expected_protected, "Probe protected bytes differ")
    expected_names = set(SAVES if profile == "positive" else SAVES[:1]) | {"neutral-material-report.json"}
    if profile == "positive":
        expected_names |= {name + ".png" for name in CAPTURES}
    else:
        expected_names |= cold_files(output, material, data, aliases)
    require({row["relative_path"] for row in native_rows} == expected_names, "Unexpected profile file closure")
    return native


def cold_files(output: Path, material: Path, data: dict, aliases: dict[Path, Path]) -> set[str]:
    rows = data["cold"]["rejections"]
    require([row["id"] for row in rows] == list(COLD_IDS) and len(rows) == 24 and data["cold"]["variant_catalog_count"] == 20 and
            data["cold"]["captures"] == 0 and data["cold"]["registered_count"] == 2 and data["cold"]["saved_world_exact"] is True and
            data["captures"] == [] and data["zoom_cases"] == [] and data["capture_limit"] == 0, "Cold controls or GPU scope differs")
    expected_alias_rows = [{"path": path.relative_to(output).as_posix(), "target": target.as_posix(), "owned": True,
                            "target_exact": True, "followed_in_inventory": False} for path, target in sorted(aliases.items())]
    require(data["retained_junctions"] == expected_alias_rows, "Retained exact junction ledger differs")
    expected = set()
    for row in rows:
        require(type(row["error"]) is str and row["error"] and row["diagnostic_contains"] in row["error"] and
                row["frame_camera_pick_save_exact"] is True and row["before_frame_sha256"] == row["after_frame_sha256"], "Cold atomic rejection differs")
        label = "palette-reread-oversize" if row["id"].startswith("palette-reread-oversize-") else row["id"]
        wanted = "Unknown asset ID: " + next(item["namespace"] for item in load(material / "art/material-collection.json")["items"] if item["legacy_key"] == KEYS[0]) + ".palette" if label == "palette-renamed-id" else COLD_DIAGNOSTICS[label]
        require(row["diagnostic_contains"] == wanted and wanted in row["error"], "Cold control missed its intended validation")
        if "root" not in row:
            require(row["id"] in COLD_IDS[:2], "Missing cold fixture root")
            continue
        root = output / row["root"]
        require(row["root"] == "invalid-" + ("palette-reread-oversize" if row["id"].startswith("palette-reread-oversize-") else row["id"]), "Cold fixture ID/root differs")
        if row["id"] == "root-reparse":
            require(Path(row["reparse_target"]) == material and root.resolve(strict=True) == material, "Root rejection target differs")
            continue
        files = inventory(root, aliases)
        require(row["files"] == lower_rows(files), "Cold fixture bytes not bound")
        expected |= {row["root"] + "/" + value["relative_path"] for value in files}
        if row["id"].startswith("palette-reread-oversize-"):
            require(row["operation"] == row["id"].rsplit("-", 1)[1] and row["initial_constructor_passed"] is True and
                    row["copied_profile_preserved"] is True, "Reread/copy positive precursor missing")
        else:
            require(row["constructor_inside_try"] is True, "Constructor failure lies outside actual rejection control")
    verify_cold_variants(output, material, aliases)
    return expected


def verify_cold_variants(output: Path, material: Path, aliases: dict[Path, Path]) -> None:
    baseline = {row["relative_path"]: Path(row["path"]).read_bytes() for row in inventory(material)}
    accepted_metadata = load(material / "art/material-collection.json")
    accepted_world = load(material / "art/world.json")
    hi = next(i for i, item in enumerate(accepted_metadata["items"]) if item["legacy_key"] == KEYS[0])
    mi = next(i for i, item in enumerate(accepted_metadata["items"]) if item["legacy_key"] == KEYS[1])
    palette_path = accepted_metadata["items"][hi]["albedo_source"]["palette_path"]
    for case in COLD_CASES:
        if case == "root-reparse":
            continue
        root = output / ("invalid-" + case)
        actual_files = {row["relative_path"]: Path(row["path"]).read_bytes() for row in inventory(root, aliases)}
        expected_paths = set(baseline) | ({"retained-before-palette.json"} if case == "palette-reread-oversize" else set())
        require(set(actual_files) == expected_paths, "Cold payload membership changed beyond intended mutation")
        metadata, world = load(root / "art/material-collection.json"), load(root / "art/world.json")
        house, man = metadata["items"][hi], metadata["items"][mi]
        for name, blob in baseline.items():
            if name in {"manifest.json", "art/material-collection.json", "art/world.json", palette_path}:
                continue
            if case == "albedo_byte" and name.endswith("fixture.resident/albedo.rtex"):
                changed = bytearray(blob); changed[0] ^= 1
                require(actual_files[name] == bytes(changed), "Painted albedo failure does not mutate exact byte0")
            else:
                require(actual_files[name] == blob, "Cold fixture changed an unrelated payload")
        if case in {"palette-declared-oversize", "palette-actual-oversize"}:
            require(len(actual_files[palette_path]) == 524289 and actual_files[palette_path].startswith(baseline[palette_path]) and
                    actual_files[palette_path][len(baseline[palette_path]):] == b" " * (524289 - len(baseline[palette_path])), "Oversized constructor fixture differs")
        elif case == "palette-reread-oversize":
            require(actual_files[palette_path] == b" " * 524289 and actual_files["retained-before-palette.json"] == baseline[palette_path], "Reread growth did not preserve initial palette")
        else:
            palette = load(root / palette_path)
            expected_palette = json.loads(baseline[palette_path])
            if case == "palette-scalar":
                expected_palette["materials"][0]["roughness"] = True
            elif case == "palette-schema":
                expected_palette["schema"] = 2
            elif case == "notice-reparse":
                expected_palette["draft_notice"]["path"] = "notice-alias/" + Path(house["albedo_source"]["provenance"]["notice_path"]).name
            require(palette == expected_palette, "Cold palette mutation differs")
        expected_metadata = copy.deepcopy(accepted_metadata)
        expected_world = copy.deepcopy(accepted_world)
        eh, em = expected_metadata["items"][hi], expected_metadata["items"][mi]
        if case == "schema": expected_world["schema"] = 3
        elif case == "reference_width": em["registration"]["reference_width"] = 139.
        elif case == "pivot": em["registration"]["pivot_px"][0] = 1.
        elif case == "decoded_crop_hash": em["registration"]["decoded_crop_sha256"] = "0" * 64
        elif case == "legacy_key": em["legacy_key"] = "resident.man.walk.unknown"
        elif case == "neutral-null": eh["albedo_source"] = None
        elif case == "neutral-extra": eh["albedo_source"]["extra"] = True
        elif case == "authored-provenance": eh["albedo_source"]["provenance"]["author"] = "Different author"
        elif case == "coverage": eh["coverage"]["original_alpha_samples"] = 0
        elif case == "generated-sha": eh["generated_albedo_sha256"] = "0" * 64
        elif case == "notice-reparse": eh["albedo_source"]["provenance"]["notice_path"] = "notice-alias/" + Path(eh["albedo_source"]["provenance"]["notice_path"]).name
        elif case == "palette-internal-reparse": eh["albedo_source"]["palette_path"] = "palette-alias/" + Path(palette_path).name
        if case in {"palette-scalar", "palette-schema", "notice-reparse"}:
            eh["albedo_source"]["palette_sha256"] = hashlib.sha256(actual_files[palette_path]).hexdigest()
        if case in {"reference_width", "pivot", "decoded_crop_hash", "neutral-null", "neutral-extra", "authored-provenance",
                    "generated-sha", "palette-scalar", "palette-schema", "notice-reparse", "palette-internal-reparse"}:
            for item in (eh, em):
                item["source_hash"] = source_hash(source_record(item))
                next(value for value in expected_world["materials"] if value["id"] == item["material_id"])["source_hash"] = item["source_hash"]
        if case == "albedo_byte":
            texture = next(texture for texture in expected_world["textures"] if texture["path"].endswith("fixture.resident/albedo.rtex"))
            texture["sha256"] = hashlib.sha256(actual_files[texture["path"]]).hexdigest()
        require(metadata == expected_metadata and world == expected_world, "Cold metadata/source mutation differs")
        manifest = load(root / "manifest.json")
        manifest_content(manifest)
        accepted_manifest = load(material / "manifest.json")
        expected_manifest = copy.deepcopy(accepted_manifest)
        palette_id = accepted_metadata["items"][hi]["namespace"] + ".palette"
        for entry in expected_manifest["assets"]:
            if case == "palette-renamed-id" and entry["id"] == palette_id:
                entry["id"] = accepted_metadata["items"][hi]["namespace"] + ".renamed"
            if case == "palette-internal-reparse" and entry["id"] == palette_id:
                entry["path"] = "palette-alias/" + Path(palette_path).name
            if case == "notice-reparse" and entry["notice"] == accepted_metadata["items"][hi]["albedo_source"]["provenance"]["notice_path"]:
                entry["notice"] = house["albedo_source"]["provenance"]["notice_path"]
            path = entry["path"]
            blob = baseline[palette_path] if case in {"palette-reread-oversize", "palette-internal-reparse"} and entry["id"] == palette_id else actual_files[path]
            entry["sha256"] = hashlib.sha256(blob).hexdigest()
            entry["bytes"] = 524288 if case == "palette-actual-oversize" and entry["id"] == palette_id else len(blob)
        expected_manifest["assets"].sort(key=lambda entry: entry["id"])
        expected_manifest["content_sha256"] = manifest["content_sha256"]
        require(manifest == expected_manifest, "Cold manifest changes differ from exact rebound fixture")


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
    dimensions = []
    for page in world["pages"]:
        for sprite in page["sprites"]:
            w, h = sprite["rect"][2:]
            scale = min(1., f32(192. / max(w, h)))
            dimensions.append([max(1, math.floor(f32(w * scale) + .5)), max(1, math.floor(f32(h * scale) + .5))])
    require(legacy["hit_mask_bytes"] == sum(w * h for w, h in dimensions) and legacy["hit_mask_count"] == len(dimensions) == 987 and
            legacy["outline_texture_bytes"] == legacy["outline_sprite_count"] == 0, "Original retained-mask counters differ")
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
    require([row["id"] for row in data["captures"]] == list(CAPTURES) and data["capture_limit"] == 20 and
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
    for name in ("neutral-body-default", "neutral-original-default", "neutral-world-restored", "neutral-world-hud"):
        pixels = Counter(images[name].crop((320, 200, 1080, 650)).getdata())
        nonmodal = 760 * 450 - max(pixels.values())
        require(len(pixels) > 64 and nonmodal > 10000, "Actual World scene is uniform/vacuous")
        visible[name] = {"unique_rgba": len(pixels), "nonmodal_pixels": nonmodal}
    menu = images["neutral-menu-first"]
    require(len(set(menu.convert("RGB").getdata())) == data["ui"]["menu_first"]["unique_rgb"] > 100, "Actual menu pixels are vacuous")
    zooms = data["zoom_cases"]
    require([row["id"] for row in zooms] == ["default", "min", "max"] and len(zooms) == 3, "Zoom controls differ")
    cache = zooms[0]["outline"]["texture"]
    for row, zoom, wheel in zip(zooms, (f32(1.3), f32(.62), 3.), (0., -100., 100.), strict=True):
        require(row["zoom"] == zoom and row["wheel_delta"] == wheel and row["typed_material_draws"] == 2 and row["cache_handle_equal"] is True,
                "Ordinary wheel/clamp control differs")
        query(sources[KEYS[0]], row["house"], zoom)
        query(sources[KEYS[1]], row["resident"], zoom)
        body_id, outline_id = "neutral-body-" + row["id"], "neutral-outline-" + row["id"]
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
        require(changed("neutral-original-default", "neutral-body-default", zooms[0][query_name]["bounds"]) > 0, "Mapped native route change is vacuous")
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
        require(row["mixed_vs_day_changed_pixels"] == changed("neutral-mixed-" + row["id"], "neutral-body-default") > 0 and
                row["legacy_native_capture"] is False, "Authority pixel/scope differs")
        if row["id"] == "wet":
            require(row["legacy_wet_frame_exact"] is True and row["legacy_frame_sha256"] == row["day_legacy_frame_sha256"] == by_id["neutral-original-default"]["frame_sha256"] and
                    row["legacy_vs_day_changed_pixels"] == 0 and row["legacy_vs_mixed_changed_pixels"] == changed("neutral-original-default", "neutral-mixed-wet") > 0,
                    "Wet-only original fallback changed")
        else:
            require(row["legacy_vs_day_changed_pixels"] is row["legacy_vs_mixed_changed_pixels"] is None and row["legacy_wet_frame_exact"] is False,
                    "Uncaptured night legacy pixels were invented")
    mine, unavailable = authority[2:]
    require(mine["pixels_equal"] is True and mine["hdr"] is False and mine["typed_material_draws"] == 0 and
            mine["mixed_frame_sha256"] == mine["baseline_frame_sha256"] and changed("neutral-mine-fallback", "neutral-mine-legacy") == 0,
            "Mine original fallback differs")
    require(unavailable["remote"] is True and all(unavailable[key] is False for key in ("ready", "world_ready", "weather_known", "hdr")) and
            unavailable["typed_material_draws"] == 0 and unavailable["mixed_frame_sha256"] == unavailable["baseline_frame_sha256"] and
            images["neutral-authority-unavailable"].getextrema() == ((139, 139), (158, 158), (171, 171), (255, 255)),
            "Pending unknown authority invented content")
    two = data["two_selected"]
    require(two["selected_buildings"] == [2, 4] and len(two["outlines"]) == len(two["bodies"]) == len(two["points"]) == 2 and
            two["repeat_frame_exact"] is two["cache_handle_equal"] is True and two["added_vertices"] == 64 and two["added_indices"] == 96 and
            two["intervening_plain_commands"] > 0 and two["selected_frame_contract"] == by_id["neutral-two-houses-selected"]["frame_contract"] and
            two["selected_frame_contract"]["vertex_count"] == two["clean_frame_contract"]["vertex_count"] + 64 and
            two["selected_frame_contract"]["index_count"] == two["clean_frame_contract"]["index_count"] + 96 and
            two["capture_rgba_sha256"] == hashlib.sha256(images["neutral-two-houses-selected"].tobytes()).hexdigest(), "Two selected mapped houses are vacuous")
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
    require(two["outlines"][1]["command"] > two["bodies"][0]["command"], "Intervening body/outline order differs")
    ui = data["ui"]
    require(ui["hud"]["changed_pixels"] == changed("neutral-body-default", "neutral-world-hud") > 1000 and
            ui["hud"]["world_prefix_sha256"] == by_id["neutral-body-default"]["frame_sha256"] and
            ui["interleave"]["menu_actual_submission"] is True and ui["interleave"]["menu_pixel_repeat_measured"] is False and
            ui["interleave"]["world_pixels_equal"] is True and ui["interleave"]["before_frame_sha256"] == ui["interleave"]["after_frame_sha256"] ==
            by_id["neutral-world-restored"]["frame_sha256"] == by_id["neutral-body-default"]["frame_sha256"] and
            changed("neutral-body-default", "neutral-world-restored") == 0, "Actual RmlUi/prefix/interleave differs")
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
    return {"visible_world": visible, "captures": 20, "typed_zoom_queries": 6, "outline_offsets": 8,
            "two_selected_entities": 2, "direct_api_controls": 15, "invalid_cache_limits": 2}


def verify_body_visibility(row: dict, source: dict, query_row: dict, images: dict, captures: dict, changed) -> None:
    require(row["id"] == "same_frame_house_body_removal" and row["source_capture"] == "neutral-body-default.png" and
            row["removed_capture"] == "neutral-body-default-house-removed.png" and row["only_house_command_removed"] is row["carrier_payload_exact"] is True,
            "Paired body identity/scope differs")
    original, removed = row["source_frame"], row["removed_frame"]
    expected = copy.deepcopy(original)
    index = row["removed_command_index"]
    require(type(index) is int and 0 <= index < len(expected["commands"]) and expected["commands"][index] == {"geometry": row["removed_command"]},
            "Body removal targeted another command")
    del expected["commands"][index]
    require(removed == expected and len(original["sprites"]) == len(removed["sprites"]) == 2 and len(original["surfaces"]) == len(original["vertices"]),
            "Paired body removal changed vertex/material/lighting/companion payload")
    require(row["source_frame_sha256"] == captures["neutral-body-default"]["frame_sha256"] and
            row["removed_frame_sha256"] == captures["neutral-body-default-house-removed"]["frame_sha256"] and
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
    before = images["neutral-body-default"].tobytes()
    after = images["neutral-body-default-house-removed"].tobytes()
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
            row["total_changed_pixels"] == changed("neutral-body-default", "neutral-body-default-house-removed") and
            row["changed_pixels_in_bounds"] == changed("neutral-body-default", "neutral-body-default-house-removed", row["bounds"]) >= different,
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


def verify_common(data: dict, build: Path, ui: Path, material: Path, painted: Path, profile: str) -> dict[str, dict]:
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
            "painted_content_hash": manifest_content(load(painted / "manifest.json")),
            "read_profiles": {"original": 0, "neutral": 1, "painted": 0, "ui": 0},
            "ui_content_hash": manifest_content(load(ui / "manifest.json")),
            "original_world_sha256": digest(original / "art/world.json"),
            "collection_metadata_sha256": digest(material / "art/material-collection.json"),
            "collection_world_sha256": digest(material / "art/world.json")} and world["schema"] == 1 and
            len(world["pages"]) == 67 and sum(len(page["sprites"]) for page in world["pages"]) == 987, "Input identities differ")
    require(not any("accepted" == item.lower() for item in data["open_delivery"]), "Native scope invents acceptance")
    if profile == "cold":
        require(data["scope"] == "Cold MaterialCollection actual constructor/read/View rejections before submission, then current all-painted Default compatibility normal submission without PNG; no physical allocation-fault claim" and
                data["open_delivery"] == ["Positive GPU proof in separate profile", "Physical GPU OOM and bad_alloc", "Production/reference acceptance",
                "Full E01-E17/P01", "Normal main startup", "FPS/GPU timing"], "Cold scope differs")
    else:
        require(data["scope"] == "Actual normal GameView::compose LegacyCombined provisional XYZ palette house plus unchanged diagnostic painted resident; art/reference/whole-engine acceptance OPEN" and
                all(claim in data["open_delivery"] for claim in ("Full E01-E17/P01 engine acceptance", "full normal main startup and package/release",
                "production neutral albedo and all eight authored material masters", "reference fidelity", "FPS and GPU execution timing",
                "physical GPU OOM/bad_alloc/plain-outline injection", "normal GameView mirrored fallback untriggered with canonical full987 catalog")),
                "Positive diagnostic overstates scope")
    return collection_sources(material, data)


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
    for name in ("build", "objects", "kit", "ui", "material", "painted"):
        parser.add_argument(name, type=Path)
    parser.add_argument("profile", choices=("positive", "cold"))
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    args = parser.parse_args()
    paths = {name: getattr(args, name).resolve(strict=True) for name in ("build", "objects", "kit", "ui", "material", "painted", "first", "second")}
    require(os.name == "nt" and len(set(paths.values())) == len(paths), "Eight distinct local Windows roots required")
    for name, path in paths.items():
        no_reparse(path)
        require(path.is_dir() and path.is_relative_to(REPOSITORY / "build") and
                (name in {"kit", "ui", "material", "painted"} or path.parent == REPOSITORY / "build"), "Invalid retained evidence root")
    build, objects, kit, ui, material, painted, first, second = (paths[key] for key in ("build", "objects", "kit", "ui", "material", "painted", "first", "second"))
    guarded = [build / "build-report.json", build / "artifact-storage.json", Path(__file__).resolve(strict=True),
               first / "native-execution.json", second / "native-execution.json", first / "neutral-material-report.json", second / "neutral-material-report.json"]
    initial = [(str(path), path.stat().st_size, digest(path)) for path in guarded]
    report, checked = verify_build(build, objects, kit, ui, material, painted)
    frozen_reader = next(row for row in report["source_provenance"] if row["relative_path"] == "tests/gameview_neutral_material_evidence_tests.py")
    require(Path(frozen_reader["repository_path"]) == Path(__file__).resolve(strict=True) and frozen_reader["sha256"].lower() == digest(Path(__file__)),
            "Read-only verifier differs from actual compiled snapshot")
    natives, witnesses, negative_count = [], [], 0
    for output in (first, second):
        native = verify_native(build, objects, kit, ui, material, painted, args.profile, output, report, set(checked))
        sources = verify_common(native["deterministic"], build, ui, material, painted, args.profile)
        if args.profile == "cold":
            verify_painted_default(painted, native["deterministic"]["painted_default_compatibility"])
        witnesses.append(verify_positive(output, native["deterministic"], sources) if args.profile == "positive" else
                         {"cold_controls": 24, "variant_catalogs": 20, "captures": 0})
        negative_count += semantic_negatives(native, sources)
        natives.append(native)
    require(repeat_semantics(natives[0]) == repeat_semantics(natives[1]), "Repeated deterministic profile reports differ")
    require(witnesses[0] == witnesses[1], "Independent repeated semantic witnesses differ")
    first_files = natives[0]["deterministic"]["output_inventory_excluding_report"]
    for row in first_files:
        name = row["path"]
        if name in SAVES:
            # World checksums and saved-world-preservation qualify identity;
            # compressed save envelopes contain independent random tokens.
            continue
        require((first / name).read_bytes() == (second / name).read_bytes(), "Repeated deterministic file differs: " + name)
    require(initial == [(str(path), path.stat().st_size, digest(path)) for path in guarded], "Read-only verification mutated guarded evidence")
    print(f"Neutral material {args.profile} evidence passed; sources={len(report['source_provenance'])}; "
          f"zero_commands=122; object_files=24; individual_root_limit=134217728; "
          f"aggregate_bytes={total(inventory(build)) + total(inventory(objects))}; semantic_negative_controls={negative_count}; witnesses={witnesses[0]}")


if __name__ == "__main__":
    main()
