"""Read-only bindings and source-derived normal material composition evidence."""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import math
import struct
from pathlib import Path
from gameview_cumulative_evidence_tests import bound_rows, digest, hash_row, load, require
from gameview_ui_evidence_tests import verify_ui_kit, STRICT_FLAGS, TRANSLATION_UNITS as UI_TRANSLATION_UNITS
from gameview_material_semantics_tests import verify_semantics
TRANSLATION_UNITS = (UI_TRANSLATION_UNITS - {"tools/gameview_ui_probe.cpp"}) | {"tools/gameview_material_probe.cpp"}
WORLD_SAVES = {f"material-{name}.save" for name in ("day", "night", "wet", "variants", "winter")}
CAPTURE_IDS = (
    "material-menu-first", "material-legacy-default", "material-mixed-default",
    "material-legacy-min", "material-mixed-min", "material-legacy-max", "material-mixed-max",
    "material-legacy-night", "material-mixed-night", "material-mixed-wet", "material-world-hud",
    "material-world-selected", "material-menu-interleave", "material-world-restored", "material-variants",
    "material-winter", "material-animation-fallback", "material-mine-fallback", "material-mine-legacy",
    "material-authority-unavailable",
)
NEGATIVE_IDS = {"schema", "reference_width", "pivot", "decoded_crop_hash", "legacy_key", "albedo_byte"}
COLLECTION_PATHS = {"manifest.json", "material-collection-report.json", "NOTICE.txt", "licenses/Ribterra-Assets.txt",
                    "art/world.json", "art/material-collection.json"} | {
    f"art/materials/fixture.{item}/{channel}.rtex" for item in ("house", "resident")
    for channel in ("albedo", "normal-material", "camera-depth", "emission-mask", "surface-mask", "caster-mask")}
NATIVE_OUTPUTS = WORLD_SAVES | {f"{name}.png" for name in CAPTURE_IDS} | {"material-report.json"} | {
    f"invalid-{case}/{name}" for case in NEGATIVE_IDS for name in COLLECTION_PATHS}


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

def verify_bindings(build: Path, kit: Path, ui_kit: Path, ui_repeat: Path, material: Path, first: Path, second: Path) -> tuple[list[dict], dict]:
    build, kit, ui_kit, ui_repeat, material, first, second = (p.resolve(strict=True) for p in (build, kit, ui_kit, ui_repeat, material, first, second))
    require(first != second and first != build and second != build, "Distinct build and native roots required")
    report = load(build / "build-report.json")
    repository = Path(report["authoritative_repository"])
    require(repository == Path(r"C:\Users\fipst\Desktop\ribterra"), "Authoritative repository differs")
    require(Path(report["output_root"]) == build and Path(report["control_kit"]["root"]) == kit,
            "Build/kit binding mismatch")
    require(report["build_label"] == "gameview-material-isolated-unversioned" and
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
            len(report["material_collection"]["files"]) == 18 and
            report["original_art"]["bulk_staged"] is False and
            report["material_collection"]["bulk_staged"] is False and
            report["material_collection"]["production_accepted"] is False,
            "Normal composition original/material closure or scope differs")
    for section, root, count in (("original_art", repository / "assets", 181), ("material_collection", material, 18)):
        rows = report[section]["files"]
        require(len(rows) == count and len({r["relative_path"] for r in rows}) == count and
                {r["relative_path"] for r in rows} == {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()} and
                all(Path(r["path"]) == root / r["relative_path"] for r in rows), "Exact referenced asset tree differs")
        for row in rows:
            checked_path = Path(row["path"])
            for ancestor in (checked_path, *checked_path.parents):
                require(not ancestor.is_symlink() and not ancestor.is_junction(), "Referenced asset traverses reparse path")
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
        "tools/gameview_material_probe.cpp", "tools/engine_control_probe.hpp",
        "licenses/Cormorant-OFL.txt", "licenses/Noto-OFL.txt", "licenses/PTSerif-OFL.txt", "licenses/Ribterra-Assets.txt",
        "tools/export_engine_ui_kit.py", "tools/asset_tool.py", "src/ui/game_ui.cpp",
        "include/ribterra/render_frame.hpp", "src/render/renderer2d.cpp",
        *{f"src/ui/game_ui_{name}.inc" for name in ("lobby", "manual", "settlement", "economy", "buildings", "professions")},
        "tools/build_gameview_material_isolated.ps1", "tools/run_gameview_material_native.ps1",
        "tools/export_engine_control_kit.py", "tools/export_material_master.py",
        "tests/gameview_material_evidence_tests.py", "tests/gameview_cumulative_evidence_tests.py",
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
                      '/DRIBTERRA_PROBE_BUILD="gameview-material-isolated-unversioned"',
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
    expected_objects = {build / "obj" / (Path(name).stem + ".obj") for name in TRANSLATION_UNITS}
    require(len(report["objects"]) == 24 and object_paths == expected_objects, "Bound object inventory differs")
    compiled_sources = set()
    for row in compiles:
        argv = row["arguments"]
        require(STRICT_FLAGS <= set(argv) and "/WX-" not in argv and argv.count("/c") == 1,
                "Actual compile lacks strict flags")
        source = Path(argv[argv.index("/c") + 1])
        unit_flags = (["/Zc:inline"] if source.name == "gameview_material_probe.cpp" else
                      ["/DSTEAMNETWORKINGSOCKETS_STATIC_LINK"] if source.name == "gns_transport.cpp" else [])
        require(argv == expected_flags + unit_flags + ["/c", str(source),
                "/Fo" + str(build / "obj" / (source.stem + ".obj"))],
                "Actual compile arguments contain unbound inputs or overriding options")
        require(argv.count("/Zc:inline") == (1 if source.name == "gameview_material_probe.cpp" else 0) and
                "/Zc:inline-" not in argv and "/GL" not in argv,
                "Actual probe-only object footprint flags differ")
        compiled_sources.add(source)
        fo = [arg for arg in argv if arg.startswith("/Fo")]
        require(fo == ["/Fo" + str(build / "obj" / (source.stem + ".obj"))], "Actual compile object output differs")
    require(compiled_sources == {build / "source" / name for name in report["translation_units"]},
            "Actual compiled sources differ from the bound TU list")
    links = [row for row in commands if Path(row["executable"]).name.lower() == "link.exe"]
    require(len(links) == 1 and links[0]["arguments"] ==
        ["/nologo", "/machine:x64", "/incremental:no", "/debug:none", "/subsystem:console",
         "/out:" + str(build / "GameViewMaterialProbe.exe")] + [row["path"] for row in report["objects"]] +
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
    exe = build / "GameViewMaterialProbe.exe"
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
    require(Path(report["ui_kit"]["root"]) == ui_kit and not report["ui_kit"]["bulk_staged"], "Separate UI kit binding differs")
    for root in (ui_kit, ui_repeat):
        verify_ui_kit(repository, root, checked)
    ui_names = {p.relative_to(ui_kit).as_posix() for p in ui_kit.rglob("*") if p.is_file()}
    require(len(ui_names) == 52 and {row["relative_path"] for row in report["ui_kit"]["files"]} == ui_names and
            all(Path(row["path"]) == ui_kit / row["relative_path"] for row in report["ui_kit"]["files"]), "UI kit inventory linkage differs")
    require(ui_names == {p.relative_to(ui_repeat).as_posix() for p in ui_repeat.rglob("*") if p.is_file()}, "UI kit repeat inventory differs")
    for name in ui_names:
        require((ui_kit / name).read_bytes() == (ui_repeat / name).read_bytes(), f"UI kit repeat bytes differ: {name}")
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
    results = []
    for root in (first, second):
        execution = load(root / "native-execution.json")
        require(execution["accepted"] is True and execution["actual_subprocess"] is True and
                execution["exit_code"] == 0 and execution["bound_inputs_unchanged"] is True and
                not execution["integrity_errors"], "Actual native execution rejected")
        require(Path(execution["build_root"]) == build and Path(execution["kit_root"]) == kit and
                Path(execution["output_root"]) == root and
                execution["actual_argv"] == [str(exe), str(build / "assets"), str(repository / "assets"), str(material), str(ui_kit), str(root)],
                "Native roots/actual argv differ")
        require(Path(execution["bound_inputs_before"]["runner"]["path"]) ==
                Path(report["authoritative_repository"]) / "tools/run_gameview_material_native.ps1",
                "Native UI runner differs")
        bound_rows(execution, checked)
        expected_bindings = {
            "executable": exe, "build_report": build / "build-report.json",
            "fresh_shader_manifest": build / "assets/shaders/manifest.json",
            "kit_manifest": kit / "manifest.json",
            "runner": repository / "tools/run_gameview_material_native.ps1",
            "original_manifest": repository / "assets/manifest.json",
            "material_manifest": material / "manifest.json",
            "material_metadata": material / "art/material-collection.json",
            "ui_manifest": ui_kit / "manifest.json", "ui_export_report": ui_kit / "ui-kit-report.json",
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
                         ("runner", source_by_name["tools/run_gameview_material_native.ps1"])):
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
        native = load(root / "material-report.json")
        require(native["schema"] == 1 and native["complete"] is True, "Native report incomplete")
        require(native["deterministic"]["inputs"]["ui_content_hash"] ==
                load(ui_kit / "manifest.json")["content_sha256"], "Actual UI catalog content identity differs")
        measured = native["measured"]
        require(set(measured) == {"total_ms"} and
                all(type(v) in (float, int) and math.isfinite(v) and v >= 0 for v in measured.values()),
                "Invalid or unbound varying diagnostic")
        for row in native["deterministic"]["captures"]:
            name = row["file"]
            header = (root / name).read_bytes()[:24]
            require(len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR" and
                    list(struct.unpack(">II", header[16:24])) == [row["width"], row["height"]], "Native PNG dimensions differ")
        require(Path(execution["ui_kit_root"]) == ui_kit, "Native UI kit linkage differs")
        for prefix, tree in (("control", kit), ("ui", ui_kit), ("original", repository / "assets"), ("material", material)):
            before_tree = execution["asset_trees"][f"{prefix}_before"]
            after_tree = execution["asset_trees"][f"{prefix}_after"]
            actual_tree = {p.relative_to(tree).as_posix() for p in tree.rglob("*") if p.is_file()}
            require(len(before_tree) == len(after_tree) == len(actual_tree) and
                    len({row["relative_path"] for row in before_tree}) == len(before_tree) and
                    len({row["relative_path"] for row in after_tree}) == len(after_tree) and
                    {row["relative_path"] for row in before_tree} == actual_tree and
                    {row["relative_path"] for row in after_tree} == actual_tree and
                    all(Path(row["path"]) == tree / row["relative_path"] for row in before_tree + after_tree) and
                    {row["path"] for row in before_tree} == {row["path"] for row in after_tree}, "Native kit tree differs")
            rows_by_path = {row["path"]: row for row in before_tree}
            for row in after_tree:
                source = rows_by_path[row["path"]]
                require(row["unchanged"] is True and row["bytes"] == row["after_bytes"] == source["bytes"] and
                        row["sha256"] == row["after_sha256"] == source["sha256"], "Native kit bytes changed")
        results.append(native)
    for name in NATIVE_OUTPUTS - WORLD_SAVES - {"material-report.json"}:
        require((first / name).read_bytes() == (second / name).read_bytes(), f"Derived output repeat differs: {name}")
    require({key: value for key, value in results[0].items() if key != "measured"} ==
            {key: value for key, value in results[1].items() if key != "measured"},
            "Stable semantic material report differs")
    hash_row({"bytes": exe.stat().st_size, "sha256": digest(exe)}, exe)
    try:
        hash_row({"bytes": exe.stat().st_size, "sha256": "0" * 64}, exe)
    except AssertionError:
        pass
    else:
        raise AssertionError("Binding negative control failed")
    return results, {"sources": len(sources), "bound_files": len(checked), "zero_commands": 122,
                     "exact_derived_files": len(NATIVE_OUTPUTS - WORLD_SAVES - {"material-report.json"})}


def canonical(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def close_values(actual: list, expected: list, tolerance: float = 0.0003) -> None:
    require(len(actual) == len(expected) and all(type(v) in (int, float) and math.isfinite(v) and
            abs(v - e) <= tolerance for v, e in zip(actual, expected)), "Source-derived geometry differs")


def verify_frame_contract(capture: dict) -> None:
    frame = capture["frame_contract"]
    count_keys = ("vertex_count", "index_count", "command_count", "sprite_material_count", "companion_count",
                  "neutral_companion_count", "non_neutral_companion_count", "typed_command_count",
                  "plain_geometry_command_count", "text_command_count")
    require(all(type(frame[k]) is int and frame[k] >= 0 for k in count_keys) and
            type(frame["hdr"]) is bool and frame["content"] in (0, 1), "Frame counters are vacuous or invalid")
    if capture["id"] == "material-authority-unavailable":
        require(frame["content"] == 0 and frame["vertex_count"] == frame["index_count"] == frame["command_count"] == 0,
                "Unavailable authority emitted invented world geometry")
    else:
        require(frame["vertex_count"] > 0 and frame["index_count"] > 0 and frame["command_count"] > 0,
                "Ready world or UI geometry is vacuous")
    require(frame["typed_command_count"] == frame["sprite_material_count"] == len(frame["typed_roles"]) and
            frame["command_count"] == frame["typed_command_count"] + frame["plain_geometry_command_count"] +
            frame["text_command_count"] and frame["non_neutral_companion_count"] == 0 and
            frame["neutral_companion_count"] == frame["companion_count"], "Mixed frame roles/companions differ")
    require(frame["companion_count"] == (frame["vertex_count"] if frame["hdr"] else 0),
            "Normal HDR/legacy companion alignment differs")
    neutral = [[[0.0, 0.0, 0.0], [0.0, 0.0, 1.0]]] * frame["companion_count"]
    require(frame["companion_sha256"] == hashlib.sha256(canonical(neutral)).hexdigest(),
            "Actual full companion digest differs from source neutral SurfaceVertex stream")
    expected_two = {"material-mixed-default", "material-mixed-min", "material-mixed-max",
                    "material-mixed-night", "material-mixed-wet", "material-world-hud",
                    "material-world-selected", "material-world-restored"}
    expected_one = {"material-variants", "material-winter"}
    expected = 2 if capture["id"] in expected_two else 1 if capture["id"] in expected_one else 0
    if capture["id"] == "material-animation-fallback":
        require(frame["sprite_material_count"] in (1, 2), "Animation has no mapped house or invalid family count")
    else:
        require(frame["sprite_material_count"] == expected, "Capture's actual registered subset differs")
    require(frame["hdr"] == (frame["sprite_material_count"] > 0), "Mapped material/HDR route differs")
    roles = frame["typed_roles"]
    require(len({r["command"] for r in roles}) == len(roles), "Duplicate typed role witness")
    for role in roles:
        draw = role["draw"]
        require(len(draw) == 14 and 0 <= role["command"] < frame["command_count"] and
                draw[0:4] == [1, 1, 0, 0] and draw[5] == 6 and draw[10] is False and
                draw[11] is False and 1 <= draw[12] <= frame["sprite_material_count"] and draw[13] == 0 and
                0 <= draw[4] <= frame["index_count"] - 6, "Typed body crossed a draw/scissor/material role")
        colors = role["referenced_vertices"]
        require(len(colors) == len({c[0] for c in colors}) == 4 and
                all(len(c) == 5 and 0 <= c[0] < frame["vertex_count"] and c[1:4] == [255, 255, 255] and
                    type(c[4]) is int and 0 <= c[4] <= 255 for c in colors), "Actual typed vertices received legacy RGB")
    telemetry = capture["telemetry"]
    require(telemetry["submitted"] is True and telemetry["capture"] is True and
            telemetry["vertices"] >= frame["vertex_count"] and telemetry["indices"] >= frame["index_count"] and
            telemetry["material_draws"] == frame["sprite_material_count"] and
            telemetry["surface_draws"] == telemetry["ground_draws"] == 0 and
            telemetry["gpu_execution_ms"] is None, "Actual native frame telemetry differs from witnessed frame")
    if frame["text_command_count"] == 0:
        require(telemetry["vertices"] == frame["vertex_count"] and telemetry["indices"] == frame["index_count"],
                "Text-free authored frame unexpectedly expanded in the renderer")
    require(telemetry["graph_pass_count"] == (1 if frame["content"] == 1 else 0) and
            telemetry["graph_passes"] == ([4] if frame["content"] == 1 else []) and
            all(telemetry[key] == 0 for key in ("occluders", "occluder_upload_bytes", "contact_casters",
                "contact_draws", "contact_mask_binds", "water_draws", "reflection_draws")),
            "Normal material route used a cumulative graph")


def source_crop(original: Path, world: dict, key: str) -> tuple[dict, dict, bytes, list[int], bytes, bytes]:
    from PIL import Image
    entries = [(page, entry) for page in world["pages"] for entry in page["sprites"] if entry["id"] == key]
    require(len(entries) == 1, "Original sprite identity is missing or ambiguous")
    page, entry = entries[0]
    x, y, width, height = entry["rect"]
    with Image.open(original / page["path"]) as image:
        rgba = image.convert("RGBA").crop((x, y, x + width, y + height)).tobytes()
    scale = min(1.0, f32(192.0 / max(width, height)))
    mw = max(1, math.floor(f32(width * scale) + 0.5))
    mh = max(1, math.floor(f32(height * scale) + 0.5))
    alpha = rgba[3::4]
    pooled = bytearray()
    for cy in range(mh):
        y0, y1 = cy * height // mh, (cy + 1) * height // mh
        for cx in range(mw):
            x0, x1 = cx * width // mw, (cx + 1) * width // mw
            pooled.append(max(alpha[sy * width + sx] for sy in range(y0, max(y0 + 1, y1))
                              for sx in range(x0, max(x0 + 1, x1))))
    coverage = bytes(255 if value >= 64 else 0 for value in pooled)
    return page, entry, rgba, [mw, mh], bytes(pooled), coverage


def verify_query(original: Path, world: dict, row: dict, zoom: float) -> None:
    page, entry, rgba, mask_size, pooled, coverage = source_crop(original, world, row["key"])
    _, _, sw, sh = entry["rect"]
    pivot = [f32(value) for value in entry["pivot"]]
    reference = f32(entry.get("reference_width", sw))
    require(row["crop"] == entry["rect"] and row["reference_width"] == reference and row["pivot"] == pivot and
            row["crop_rgba_sha256"] == hashlib.sha256(rgba).hexdigest() and row["mask_size"] == mask_size and
            row["pooled_alpha_sha256"] == hashlib.sha256(pooled).hexdigest() and
            row["pooled_coverage_sha256"] == hashlib.sha256(coverage).hexdigest(), "Original crop or pooled mask differs")
    unzoomed_width = f32(76.0 * f32(2.1)) if row["building"] else 39.0
    requested = f32(unzoomed_width * zoom)
    require(abs(row["requested_width"] - requested) < 0.0001, "Normal requested width differs from original class")
    anchor = [2.0, -2.0, f32(-15.0 / 29.0)] if row["building"] else [-3.0, 3.0, f32(-6.0 / 29.0)]
    close_values(row["expected_render_anchor"], anchor, 0.000001)
    expected_scale = f32(f32(unzoomed_width * 4.0) / reference)
    require(abs(row["geometry_scale"] - expected_scale) <= 0.000001, "Registered unzoomed geometry scale differs")
    width = f32(requested * f32(sw / reference))
    height = f32(width * f32(sh / sw))
    gx, gy = row["ground"]
    bounds = [f32(gx - f32(width * pivot[0])), f32(gy - f32(height * pivot[1])), width, height]
    close_values(row["bounds"], bounds)
    x, y, w, h = row["bounds"]
    corners = [[x, y], [f32(x + w), y], [f32(x + w), f32(y + h)], [x, f32(y + h)]]
    for name, typed in (("baseline", False), ("mixed", True)):
        quad = row[name]
        require(len(quad["vertices"]) == 4 and bool(quad["material_index"]) == typed, "Actual queried body quad is vacuous")
        for vertex, corner in zip(quad["vertices"], corners):
            close_values(vertex[:2], corner)
        if typed:
            require([v[2:4] for v in quad["vertices"]] == [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]] and
                    all(v[4:7] == [255, 255, 255] for v in quad["vertices"]), "Typed local UV/RGB differs")
            surface = quad["surface"]
            close_values(surface["source_size_pivot"], [sw, sh, f32(pivot[0] * sw), f32(pivot[1] * sh)])
            require(surface["anchor_export"][3] == 4.0 and surface["emission_scale"] == 1.0,
                    "Registered paint export/emission compatibility differs")
            close_values(surface["anchor_export"][:3], row["expected_render_anchor"])
            require(abs(surface["depth_scale"][2] - row["geometry_scale"]) <= 0.000001,
                    "Typed body geometry scale differs")
        else:
            from PIL import Image
            with Image.open(original / page["path"]) as image:
                pw, ph = image.size
            rx, ry = entry["rect"][:2]
            u, v, du, dv = (f32(rx / pw), f32(ry / ph), f32(sw / pw), f32(sh / ph))
            uv = [[u, v], [f32(u + du), v], [f32(u + du), f32(v + dv)], [u, f32(v + dv)]]
            for vertex, expected in zip(quad["vertices"], uv):
                close_values(vertex[2:4], expected, 0.000001)
    samples = row["samples"]
    require(len(samples) == 2 and {s["expected_pick"] for s in samples} == {True, False}, "SDL alpha controls are vacuous")
    mw, mh = mask_size
    for sample in samples:
        px, py = sample["point"]
        sx = math.floor(f32(f32(f32(px - x) * sw) / w))
        sy = math.floor(f32(f32(f32(py - y) * sh) / h))
        cx = min(mw - 1, int(f32(f32(f32(px - x) / w) * mw)))
        cy = min(mh - 1, int(f32(f32(f32(py - y) / h) * mh)))
        require(0 <= sx < sw and 0 <= sy < sh and 0 <= cx < mw and 0 <= cy < mh and
                sample["raw_source_pixel"] == [sx, sy] and sample["raw_alpha"] == rgba[(sy * sw + sx) * 4 + 3] and
                sample["mask_cell"] == [cx, cy] and sample["max_alpha"] == pooled[cy * mw + cx] and
                sample["expected_pick"] == sample["baseline_pick"] == sample["mixed_pick"] ==
                (pooled[cy * mw + cx] >= 64), "Actual SDL selection disagrees with source-derived pooled alpha")
    require(row["half_open_baseline_pick"] is False and row["half_open_mixed_pick"] is False and
            row["half_open_point"][0] >= max(row["baseline"]["vertices"][1][0], row["mixed"]["vertices"][1][0]) - 0.0003,
            "Actual half-open selection control differs")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "kit", "ui_kit", "ui_repeat", "material", "first", "second"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    results, counts = verify_bindings(args.build, args.kit, args.ui_kit, args.ui_repeat,
                                     args.material, args.first, args.second)
    repository = Path(load(args.build / "build-report.json")["authoritative_repository"])
    witnesses = [verify_semantics(root, repository / "assets", args.material, native,
                                 verify_frame_contract, verify_query)
                 for root, native in zip((args.first, args.second), results, strict=True)]
    require(witnesses[0] == witnesses[1], "Repeated source-derived semantic witnesses differ")
    print("Normal material GameView evidence checks passed; " +
          ", ".join(f"{key}={value}" for key, value in counts.items()))


if __name__ == "__main__":
    main()
