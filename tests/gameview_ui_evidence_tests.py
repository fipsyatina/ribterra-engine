"""Read-only source, SDK, UI kit, actual native and pixel evidence for UI integration."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import posixpath
from collections import Counter
from html.parser import HTMLParser
import math
import struct
from pathlib import Path

from gameview_cumulative_evidence_tests import bound_rows, digest, hash_row, load, require
from gameview_ui_scene_evidence_tests import verify as verify_visible_world

WORLD_SAVES = {"ui-world-current.save"}
CAPTURES = {f"{name}.png" for name in (
    "ui-empty", "ui-direct-legacy", "ui-direct-cumulative", "ui-rml-menu", "ui-rml-menu-neutral",
    "ui-world-before", "ui-rml-hud", "ui-interleave-menu", "ui-world-after", "ui-flight-1", "ui-flight-2",
    "ui-flight-3", "ui-odd-resize", "ui-restored", "ui-invalid-recovered")}
QUERY_FIXTURE_ROOTS = {"typed-query-fixture", "typed-query-invalid-size", "typed-query-invalid-rect",
                      "typed-query-invalid-hash", "typed-query-invalid-budget"}
QUERY_FIXTURE_FILES = {f"art/material/{name}.rtex" for name in
                      ("albedo", "normal-material", "camera-depth", "emission-mask", "surface-mask", "caster-mask")}
QUERY_FIXTURE_FILES |= {"source.png", "art/world.json", "manifest.json", "NOTICE.txt"}
NATIVE_OUTPUTS = WORLD_SAVES | CAPTURES | {"ui-carrier.rgba8", "ui-report.json"} | {
    f"{root}/{name}" for root in QUERY_FIXTURE_ROOTS for name in QUERY_FIXTURE_FILES}
STRICT_FLAGS = {"/std:c++20", "/O2", "/MT", "/W4", "/WX", "/permissive-", "/utf-8"}
UI_PAYLOAD = {"art/world.json", "art/concept-bridge-clay.png", "art/concept-houses-low.png", "art/concept-services.png",
    "art/concept-village.png", "fonts/CormorantGaramond.ttf", "fonts/NotoSans-Regular.ttf", "fonts/PTSerif-Regular.ttf",
    "locales/en.json", "locales/es.json", "locales/pl.json", "locales/ru.json", "locales/uk.json",
    "ui/building-panels.rcss", "ui/concept.rcss", "ui/hud-v01.rcss", "ui/hud.rml", "ui/menu.rml", "ui/ribterra.rcss",
    "ui/art/theme.rcss", "ui/art/building-house.png", "ui/art/concept-hud-icons.png", "ui/art/concept-panel.png",
    "ui/art/glyph-0.png", "ui/art/glyph-1.png", "ui/art/glyph-2.png", "ui/art/glyph-4.png", "ui/art/glyph-5.png",
    "ui/art/glyph-6.png", "ui/art/glyph-7.png", "ui/art/glyph-8.png", "ui/art/glyph-9.png", "ui/art/glyph-10.png",
    "ui/art/glyph-11.png", "ui/art/glyph-12.png", "ui/art/glyph-13.png", "ui/art/icons-cursors.2.png",
    "ui/art/icons-cursors.16.png", "ui/art/icons-cursors.17.png", "ui/art/icons-cursors.19.png", "ui/art/lobby-hall.png",
    "ui/art/lobby-portraits.png", "ui/art/lobby-valley.png", "ui/art/menu-landscape.png", "ui/art/ui-kit.0.png",
    "ui/art/ui-kit.1.png", "ui/art/ui-kit.2.png", "ui/art/ui-kit.3.png", "ui/art/ui-kit.4.png", "ui/art/ui-kit.5.png"}
UI_NOTICES = {"licenses/Cormorant-OFL.txt", "licenses/Noto-OFL.txt", "licenses/PTSerif-OFL.txt", "licenses/Ribterra-Assets.txt"}
UI_INPUTS = UI_NOTICES | {"tools/export_engine_ui_kit.py", "tools/asset_tool.py", "content/asset_sources.json"}
UI_PORTRAITS = {"building." + name + ".ready" for name in
    ("house", "farm", "stockpile", "mine", "workshop", "fishing", "bridge", "lumbermill", "mill", "kitchen", "claypit")}
TRANSLATION_UNITS = {"src/foundation/" + name + ".cpp" for name in
    ("foundation", "economy", "lobby", "atomic_file", "platform")} | {
    "src/sim/world.cpp", "src/sim/weather.cpp", "src/sim/skills.cpp", "src/sim/world_save.cpp",
    "src/server/game_server.cpp", "src/net/gns_transport.cpp", "src/net/protocol.cpp", "src/net/network_session.cpp",
    "src/assets/asset_catalog.cpp", "src/assets/texture_atlas.cpp", "src/ui/localization.cpp", "src/ui/game_ui.cpp",
    "src/render/renderer2d.cpp", "src/render/contact_shadow.cpp", "src/render/sky_shadow.cpp",
    "src/client/view.cpp", "src/client/client_session.cpp", "src/client/game_art.cpp", "tools/gameview_ui_probe.cpp"}



def verify_ui_kit(repository: Path, root: Path, checked: set[Path]) -> None:
    kit_report = load(root / "ui-kit-report.json")
    require(kit_report["schema"] == 1 and kit_report["complete"] is True and
            Path(kit_report["repository_root"]) == repository and kit_report["payload_count"] == 50 and
            kit_report["output_file_count"] == 52 and kit_report["report_not_registered_as_asset"] is True,
            "Original UI kit export incomplete or scope differs")
    for prefix in ("sources", "inputs", "protected"):
        before, after = kit_report[f"{prefix}_before"], kit_report[f"{prefix}_after"]
        require(before == after and len({row["relative_path"] for row in before}) == len(before), "UI kit inputs changed")
        for row in before:
            path = repository / row["relative_path"]
            require(path.resolve().is_relative_to(repository) and not path.is_symlink(), "UI source path escapes")
            hash_row(row, path); checked.add(path)
    require(len(UI_PAYLOAD) == 50 and {row["relative_path"] for row in kit_report["sources_before"]} ==
            {"assets/" + name for name in UI_PAYLOAD} and len(kit_report["inputs_before"]) == 7 and
            {row["relative_path"] for row in kit_report["inputs_before"]} == UI_INPUTS, "Exact approved UI source/input set differs")
    copies = kit_report["copies"]
    sources = {row["relative_path"]: row for row in kit_report["sources_before"]}
    require(len(copies) == len(sources) == 50 and len({row["relative_path"] for row in copies}) == 50, "UI payload inventory differs")
    for row in copies:
        require(row["source_relative_path"] == "assets/" + row["relative_path"] and
                {key: row[key] for key in ("bytes", "sha256")} ==
                {key: sources[row["source_relative_path"]][key] for key in ("bytes", "sha256")}, "UI copy lost original binding")
        hash_row(row, root / row["relative_path"]); checked.add(root / row["relative_path"])
    rows = kit_report["output_inventory_excluding_report"]
    names = {row["relative_path"] for row in rows}
    actual = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()}
    require(len(rows) == 51 and names == {row["relative_path"] for row in copies} | {"manifest.json"} and
            actual == names | {"ui-kit-report.json"}, "UI kit output closure differs")
    for row in rows:
        hash_row(row, root / row["relative_path"]); checked.add(root / row["relative_path"])
    total = sum(p.stat().st_size for p in root.rglob("*") if p.is_file())
    require(total == kit_report["output_total_bytes"] <= 128 * 1024**2 and
            sum(row["bytes"] for row in copies) == kit_report["payload_bytes"], "UI kit storage receipt differs")
    metadata = load(repository / "content/asset_sources.json")
    manifest = load(root / "manifest.json")
    require(manifest["schema"] == 1 and len(manifest["assets"]) == 50 and
            {row["path"] for row in manifest["assets"]} == {row["relative_path"] for row in copies}, "UI manifest payload differs")
    kinds = {".ttf": "font", ".png": "texture", ".rml": "ui", ".rcss": "style", ".json": "data"}
    for row in manifest["assets"]:
        name = row["path"]
        entry = metadata["overrides"].get(name)
        if entry is None:
            require(name.split("/", 1)[0] in metadata["project_groups"], "UI asset has no ownership metadata")
            entry = {"id": "project:" + name, "license": "LicenseRef-Proprietary", "author": metadata["project_author"],
                     "source": "project", "notice": metadata["project_notice"]}
        data = (root / name).read_bytes()
        expected = {**entry, "path": name, "kind": kinds[Path(name).suffix], "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
        if expected["kind"] == "texture":
            width, height = struct.unpack(">II", data[16:24]); expected.update(width=width, height=height)
        require(row == expected, "UI manifest metadata differs from actual source provenance")
    fields = ("id", "path", "kind", "sha256", "license", "source", "author", "notice")
    canonical = b"".join(b"".join(str(len(row[field].encode("utf-8"))).encode("ascii") + b":" + row[field].encode("utf-8")
                                   for field in fields) + b"\n" for row in sorted(manifest["assets"], key=lambda row: row["id"]))
    require(hashlib.sha256(canonical).hexdigest() == manifest["content_sha256"] == kit_report["manifest"]["content_sha256"],
            "UI registry canonical content hash differs")
    notices = kit_report["notices"]
    require(len(notices) == 4 and {row["relative_path"] for row in notices} == UI_NOTICES == {row["notice"] for row in manifest["assets"]},
            "Original UI notice metadata differs")
    for row in notices:
        data = row["text"].encode("utf-8")
        require(len(data) == row["bytes"] and hashlib.sha256(data).hexdigest() == row["sha256"], "UI notice text changed")
        hash_row(row, repository / row["relative_path"])
    world = load(root / "art/world.json")
    require(len(kit_report["ready_portraits"]) == 11 and {row["id"] for row in kit_report["ready_portraits"]} == UI_PORTRAITS, "UI alpha portrait closure differs")
    for portrait in kit_report["ready_portraits"]:
        descriptors = [(page, sprite) for page in world["pages"] for sprite in page["sprites"] if sprite["id"] == portrait["id"]]
        require(len(descriptors) == 1, "UI portrait descriptor ambiguity")
        page, sprite = descriptors[0]
        require(portrait["page"] == page["path"] and portrait["rect"] == sprite["rect"] and page["path"] in names and
                portrait["alpha_mode"] == page.get("alpha_mode", ""), "UI portrait source differs")
    class References(HTMLParser):
        def __init__(self):
            super().__init__(); self.files = []
        def handle_starttag(self, tag, attributes):
            self.files.extend(("rml." + key, value) for key, value in attributes if key in ("src", "href") and value and not value.startswith("#"))
    references = []
    for name in names:
        if not name.endswith((".rml", ".rcss")): continue
        source = re.sub(r"/\*.*?\*/", "", (root / name).read_text(encoding="utf-8"), flags=re.DOTALL)
        found = []
        if name.endswith(".rml"):
            parser = References(); parser.feed(source); found += parser.files
        found += [("style.url", match[0] or match[1] or match[2]) for match in re.findall(r"url\(\s*(?:\"([^\"]+)\"|'([^']+)'|([^\s)'\"]+))\s*\)", source)]
        if name.endswith(".rcss"):
            for value in re.findall(r"(?<![-\w])src\s*:\s*([^;{}]+);", source):
                reference = value.strip().removeprefix("url(").removesuffix(")").strip().strip("\"'")
                found.append(("rcss.src", reference))
        for syntax, reference in found:
            resolved = posixpath.normpath(posixpath.join(str(Path(name).parent).replace("\\", "/"), reference))
            require(resolved in names, "UI actual reference outside asset subset")
            references.append({"owner": name, "syntax": syntax, "reference": reference, "resolved": resolved})
    require(sorted(references, key=lambda row: (row["owner"], row["syntax"], row["reference"])) == kit_report["references"] and
            [row for row in kit_report["references"] if row["syntax"] == "rcss.src"] == kit_report["rcss_src_references"] and
            kit_report["rcss_src_references"], "Actual RML/RCSS reference closure differs")
    require(kit_report["scope"] == {"menu_hud": True, "ready_portrait_count": 11, "complete_world_catalog": False,
            "research_level2_other_buildings": "OPEN", "normal_startup": "OPEN", "full_engine": "OPEN",
            "reference_fidelity": "NOT_ACCEPTED", "fps": None}, "UI export scope overstated")


def verify_bindings(build: Path, kit: Path, ui_kit: Path, ui_repeat: Path, first: Path, second: Path) -> tuple[list[dict], dict]:
    build, kit, ui_kit, ui_repeat, first, second = (p.resolve(strict=True) for p in (build, kit, ui_kit, ui_repeat, first, second))
    require(first != second and first != build and second != build, "Distinct build and native roots required")
    report = load(build / "build-report.json")
    repository = Path(report["authoritative_repository"])
    require(repository == Path(r"C:\Users\fipst\Desktop\ribterra"), "Authoritative repository differs")
    require(Path(report["output_root"]) == build and Path(report["control_kit"]["root"]) == kit,
            "Build/kit binding mismatch")
    require(report["build_label"] == "gameview-ui-isolated-unversioned" and
            report["translation_unit_count"] == 24 and
            report["internal_production_translation_unit_count"] == 23 and
            len(report["translation_units"]) == 24 and len(set(report["translation_units"])) == 24 and
            set(report["translation_units"]) == TRANSLATION_UNITS, "Fresh weather TU closure differs")
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
        "tools/gameview_ui_probe.cpp", "tools/engine_control_probe.hpp",
        "licenses/Cormorant-OFL.txt", "licenses/Noto-OFL.txt", "licenses/PTSerif-OFL.txt", "licenses/Ribterra-Assets.txt",
        "tools/export_engine_ui_kit.py", "tools/asset_tool.py", "src/ui/game_ui.cpp",
        "include/ribterra/render_frame.hpp", "src/render/renderer2d.cpp",
        *{f"src/ui/game_ui_{name}.inc" for name in ("lobby", "manual", "settlement", "economy", "buildings", "professions")},
        "tools/build_gameview_ui_isolated.ps1", "tools/run_gameview_ui_native.ps1",
        "tools/export_engine_control_kit.py", "tools/export_material_master.py",
        "tests/gameview_ui_evidence_tests.py", "tests/gameview_cumulative_evidence_tests.py",
        "tests/gameview_ui_scene_evidence_tests.py", "include/ribterra/game_art.hpp", "src/client/game_art.cpp",
        "tests/engine_control_kit_tests.py", "include/ribterra/weather_presentation.hpp",
        "include/ribterra/view.hpp", "src/client/view.cpp", "src/sim/weather.cpp",
        "src/sim/world.cpp", "src/sim/world_save.cpp", "src/client/client_session.cpp",
        "src/net/protocol.cpp", "shaders/water_ground_lighting.frag.hlsl",
        "shaders/atmosphere_material_forward.frag.hlsl",
    }
    require(len(source_names) == len(sources) and required_sources <= source_names,
            "UI source closure missing or duplicate")
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
    require(STRICT_FLAGS <= set(flags), "Strict compiler flags differ")
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
        require(argv.count("/Zc:inline") == (1 if source.name == "gameview_ui_probe.cpp" else 0) and
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
         "/out:" + str(build / "GameViewUiProbe.exe")] + [row["path"] for row in report["objects"]] +
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
    exe = build / "GameViewUiProbe.exe"
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
                execution["actual_argv"] == [str(exe), str(build / "assets"), str(kit), str(ui_kit), str(root)],
                "Native roots/actual argv differ")
        require(Path(execution["bound_inputs_before"]["runner"]["path"]) ==
                Path(report["authoritative_repository"]) / "tools/run_gameview_ui_native.ps1",
                "Native UI runner differs")
        bound_rows(execution, checked)
        expected_bindings = {
            "executable": exe, "build_report": build / "build-report.json",
            "fresh_shader_manifest": build / "assets/shaders/manifest.json",
            "kit_manifest": kit / "manifest.json",
            "runner": repository / "tools/run_gameview_ui_native.ps1",
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
                         ("runner", source_by_name["tools/run_gameview_ui_native.ps1"])):
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
        native = load(root / "ui-report.json")
        require(native["schema"] == 1 and native["complete"] is True, "Native report incomplete")
        measured = native["measured"]
        require(set(measured) == {"total_ms"} and
                all(type(v) in (float, int) and math.isfinite(v) and v >= 0 for v in measured.values()),
                "Invalid or unbound varying diagnostic")
        for row in native["deterministic"]["captures"]:
            name = row["file"]
            header = (root / name).read_bytes()[:24]
            require(len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR" and
                    list(struct.unpack(">II", header[16:24])) == [row["width"], row["height"]], "Native PNG dimensions differ")
        require((root / "ui-carrier.rgba8").stat().st_size == 1920 * 1080 * 4, "Raw carrier dimensions differ")
        require(Path(execution["ui_kit_root"]) == ui_kit, "Native UI kit linkage differs")
        for prefix, tree in (("control", kit), ("ui", ui_kit)):
            before_tree = execution["asset_trees"][f"{prefix}_before"]
            after_tree = execution["asset_trees"][f"{prefix}_after"]
            actual_tree = {p.relative_to(tree).as_posix() for p in tree.rglob("*") if p.is_file()}
            require(len(before_tree) == len(after_tree) == len(actual_tree) and
                    {row["relative_path"] for row in before_tree} == actual_tree and
                    {row["path"] for row in before_tree} == {row["path"] for row in after_tree}, "Native kit tree differs")
            rows_by_path = {row["path"]: row for row in before_tree}
            for row in after_tree:
                source = rows_by_path[row["path"]]
                require(row["unchanged"] is True and row["bytes"] == row["after_bytes"] == source["bytes"] and
                        row["sha256"] == row["after_sha256"] == source["sha256"], "Native kit bytes changed")
        results.append(native)
    for name in NATIVE_OUTPUTS - WORLD_SAVES - {"ui-report.json"}:
        require((first / name).read_bytes() == (second / name).read_bytes(), f"Derived output repeat differs: {name}")
    require({key: value for key, value in results[0].items() if key != "measured"} ==
            {key: value for key, value in results[1].items() if key != "measured"},
            "Stable semantic UI report differs")
    hash_row({"bytes": exe.stat().st_size, "sha256": digest(exe)}, exe)
    try:
        hash_row({"bytes": exe.stat().st_size, "sha256": "0" * 64}, exe)
    except AssertionError:
        pass
    else:
        raise AssertionError("Binding negative control failed")
    return results, {"sources": len(sources), "bound_files": len(checked), "zero_commands": 122,
                     "exact_derived_files": len(NATIVE_OUTPUTS - WORLD_SAVES - {"ui-report.json"})}



POSITIVE_IDS = {"UI-empty", "UI-direct-legacy-equivalence", "UI-premultiplied-scissor-text", "RML-menu-actual",
    "RML-menu-neutral", "RML-legacy-absent-stream", "RML-world-prefix-preserved", "RML-world-neutral-suffix",
    "RML-invalid-prefix-atomic", "World-UI-World", "UI-retains-world-targets", "UI-frames-in-flight",
    "UI-odd-resize-restore", "UI-settings-preserved"}
INVALID_IDS = {"CONTENT-unknown", "WORLD-default-incomplete"} | {"UI-" + name for name in (
    "hdr", "exposure", "ground", "sprite", "water", "water-normal", "atmosphere", "moon", "contact",
    "contact-projection", "occluder", "sky", "reflection", "inactive-light", "nonUi-geometry", "nonUi-text",
    "surface-lit", "non-neutral", "misaligned", "stale-texture", "NaN-vertex", "scissor", "text-size")}
PREFIX_IDS = {"RML-HDR-missing-companions", "RML-world-misaligned", "RML-UiOnly-non-neutral", "RML-UiOnly-misaligned"}
OPEN_SCOPE = {"normal main/startup integration", "production material art and reference fidelity",
    "real RmlUi on a separate Legacy renderer; direct UI oracle only", "all other UI panels/settings actions",
    "precipitation/snow/smoke/fire FX", "full-engine FPS/GPU duration/reference acceptance"}


def require_ids(rows: list[dict], expected: set[str], fields: tuple[str, ...]) -> None:
    require(len(rows) == len(expected) and {row["id"] for row in rows} == expected and
            all(row[field] is True for row in rows for field in fields), "Missing, duplicate or failed actual controls")


def query_mask(albedo: bytes, atlas: list[int], crop: list[int]) -> tuple[list[int], bytes]:
    """Derive CPU coverage from source alpha rather than the production mask cache."""
    require(len(atlas) == 2 and all(type(value) is int and value > 0 for value in atlas) and
            len(crop) == 4 and all(type(value) is int for value in crop) and
            crop[0] >= 0 and crop[1] >= 0 and crop[2] > 0 and crop[3] > 0 and
            crop[0] + crop[2] <= atlas[0] and crop[1] + crop[3] <= atlas[1] and
            len(albedo) == atlas[0] * atlas[1] * 4, "Query albedo source extent/crop differs")
    def f32(value: float) -> float:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    scale = min(1.0, f32(192.0 / max(crop[2:])))
    width, height = (max(1, math.floor(f32(value * scale) + 0.5)) for value in crop[2:])
    coverage = bytearray()
    for y in range(height):
        y0, y1 = (y * crop[3] // height, (y + 1) * crop[3] // height)
        for x in range(width):
            x0, x1 = (x * crop[2] // width, (x + 1) * crop[2] // width)
            alpha = max(albedo[((crop[1] + sy) * atlas[0] + crop[0] + sx) * 4 + 3]
                        for sy in range(y0, max(y0 + 1, y1))
                        for sx in range(x0, max(x0 + 1, x1)))
            coverage.append(255 if alpha >= 64 else 0)
    return [width, height], bytes(coverage)


def verify_query_row(root: Path, document: dict, row: dict) -> bytes:
    sprites = {sprite["id"]: sprite for sprite in document["lit_sprites"]}
    materials = {material["id"]: material for material in document["materials"]}
    textures = {texture["id"]: texture for texture in document["textures"]}
    sprite = sprites[row["id"]]
    channel = materials[sprite["material"]]["channels"]["albedo"]
    texture = textures[channel["texture"]]
    relative = texture["path"]
    require(isinstance(relative, str) and relative and "\\" not in relative and ":" not in relative and
            not relative.startswith("/") and all(part not in ("", ".", "..") for part in relative.split("/")) and
            (root / relative).resolve().is_relative_to(root.resolve()), "Query source path escapes its asset root")
    raw = (root / relative).read_bytes()
    require(texture["format"] == "rgba8_unorm_srgb" and texture["layout"] == "tight_le" and
            row["albedo_path"] == texture["path"] and
            row["albedo_sha256"] == texture["sha256"] == hashlib.sha256(raw).hexdigest() and
            texture["bytes"] == len(raw), "Query source albedo binding differs")
    atlas, crop = [texture["width"], texture["height"]], channel["rect"]
    mask_size, coverage = query_mask(raw, atlas, crop)
    mask_hash = hashlib.sha256(coverage).hexdigest()
    covered, clear = coverage.count(255), coverage.count(0)
    require(covered > 0 and clear > 0 and
            row["crop"] == crop and row["atlas_size"] == atlas and
            row["source_size"] == sprite["source_size"] == crop[2:] and
            row["pivot_px"] == sprite["pivot_px"] and
            row["export_scale"] == sprite["export_pixels_per_reference_pixel"] and
            row["mask_size"] == mask_size and row["coverage_sha256"] == mask_hash and
            row["covered"] == covered and row["clear"] == clear, "Typed query coverage differs from source alpha")
    require(row["halfopen_checks"] == {"right_rejected": True, "bottom_rejected": True,
            "left_outside_rejected": True, "top_outside_rejected": True}, "Typed query boundary controls differ")
    transforms = row["transforms"]
    require(len(transforms) == 3 and [(value["zoom"], value["scale"]) for value in transforms] ==
            [(1.0,1.0),(.625,1.5),(1.75,struct.unpack("<f",struct.pack("<f",.8))[0])],
            "Typed query transform controls differ")
    def f32(value: float) -> float:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    for value in transforms:
        require(value["ground"] == [640.0,400.0] and value["emitted_exact"] is True and
                value["coverage_sha256"] == mask_hash and value["covered"] == covered and value["clear"] == clear,
                "Typed transformed query witness differs")
        pixel_scale = f32(f32(value["zoom"] * value["scale"]) / row["export_scale"])
        expected = [f32(640.0 - f32(row["pivot_px"][0] * pixel_scale)),
                    f32(400.0 - f32(row["pivot_px"][1] * pixel_scale)),
                    f32(row["source_size"][0] * pixel_scale), f32(row["source_size"][1] * pixel_scale)]
        require(len(value["bounds"]) == 4 and all(math.isfinite(actual) and abs(actual - want) <= 0.0001
                for actual, want in zip(value["bounds"], expected, strict=True)),
                "Typed bounds differ from independently projected source metadata")
    return coverage


def verify_typed_queries(root: Path, kit: Path, value: dict) -> None:
    from PIL import Image
    require(set(value) == {"scope", "threshold", "max_dimension", "budgets", "controls", "invalid_controls",
            "control_catalog", "rows", "fixture", "catalog_rejections", "fixtures"} and
            value["scope"] == "Diagnostic typed bounds/CPU picking; legacy outline preserved; typed outlines and normal GameView integration OPEN" and
            value["threshold"] == 64 and value["max_dimension"] == 192 and
            value["budgets"] == {"hit": 16777216, "outline": 20971520}, "Typed query schema/policy differs")
    positive = {"Q-control-bounds", "Q-control-coverage", "Q-crop-bounds", "Q-crop-alpha", "Q-half-open",
                "Q-legacy-mirror", "Q-cache-outline", "Q-cold-reload", "Q-candidate-atomic", "Q-startup-only",
                "Q-api-atomic", "Q-quantized-zero"}
    invalid = {"Q-ground-NaN", "Q-ground-infinity", "Q-zoom-NaN", "Q-zoom-infinity", "Q-zoom-zero",
               "Q-zoom-negative", "Q-scale-NaN", "Q-scale-infinity", "Q-scale-zero", "Q-scale-negative",
               "Q-underflow", "Q-overflow", "Q-collapsed", "Q-point-NaN", "Q-point-infinity",
               "Q-unknown-bounds", "Q-unknown-hit", "Q-unknown-draw", "Q-draw-anchor", "Q-draw-wetness",
               "Q-draw-opacity", "Q-draw-HDR", "Q-draw-zero-invalid-geometry"}
    require_ids(value["controls"], positive, ("passed",))
    require_ids(value["invalid_controls"], invalid, ("rejected", "unchanged"))
    require(all(isinstance(row["error"], str) and row["error"] and isinstance(row["apis"], list) and
                row["apis"] and len(row["apis"]) == len(set(row["apis"])) and
                all(isinstance(api, str) and api for api in row["apis"]) for row in value["invalid_controls"]),
            "Actual typed query rejection APIs/errors absent")
    projection_ids = {"Q-ground-NaN", "Q-ground-infinity", "Q-zoom-NaN", "Q-zoom-infinity", "Q-zoom-zero",
                      "Q-zoom-negative", "Q-scale-NaN", "Q-scale-infinity", "Q-scale-zero", "Q-scale-negative",
                      "Q-underflow", "Q-overflow", "Q-collapsed"}
    for row in value["invalid_controls"]:
        expected_apis = {"bounds", "hit", "draw"} if row["id"] in projection_ids else {
            "hit" if row["id"] in {"Q-point-NaN", "Q-point-infinity", "Q-unknown-hit"} else
            "bounds" if row["id"] == "Q-unknown-bounds" else "draw"}
        require(set(row["apis"]) == expected_apis, "Invalid query did not exercise each declared public API")
    control = load(kit / "art/world.json")
    expected_ids = {"control." + category + ".sprite" for category in
                    ("house", "stockpile", "workshop", "tree", "rock", "resident.empty", "resident.log", "resident.sack")}
    rows = value["rows"]
    require(len(rows) == 8 and {row["id"] for row in rows} == expected_ids, "Typed control category coverage differs")
    coverages = [verify_query_row(kit, control, row) for row in rows]
    require(value["control_catalog"] == {"ready": False, "legacy_contains": False, "hit_mask_count": 8,
            "hit_mask_bytes": sum(map(len, coverages)), "outline_count": 0, "outline_bytes": 0},
            "Typed-only catalog routing/cache budgets differ")
    fixture_rows = value["fixtures"]
    require(len(fixture_rows) == 5 and {row["root"] for row in fixture_rows} == QUERY_FIXTURE_ROOTS,
            "Typed diagnostic fixture roots differ")
    for row in fixture_rows:
        folder = root / row["root"]
        files = row["files"]
        require(len(files) == 10 and {file["path"] for file in files} == QUERY_FIXTURE_FILES,
                "Typed diagnostic fixture file inventory differs")
        for file in files:
            hash_row(file, folder / file["path"])
        manifest = load(folder / "manifest.json")
        records = manifest["assets"]
        require(manifest["schema"] == 1 and len(records) == 8 and
                {record["path"] for record in records} == QUERY_FIXTURE_FILES - {"manifest.json", "NOTICE.txt"} and
                len({record["id"] for record in records}) == 8, "Typed fixture manifest registration differs")
        canonical = ""
        for record in sorted(records, key=lambda record: record["id"]):
            hash_row(record, folder / record["path"])
            for key in ("id", "path", "kind", "sha256", "license", "source", "author", "notice"):
                text = record[key]
                require(isinstance(text, str) and text and record["notice"] == "NOTICE.txt", "Typed fixture provenance absent")
                canonical += f"{len(text.encode('utf-8'))}:{text}"
            canonical += "\n"
        require(hashlib.sha256(canonical.encode("utf-8")).hexdigest() ==
                manifest["content_sha256"] == row["manifest_content_sha256"], "Typed fixture content hash differs")
    valid_root = root / "typed-query-fixture"
    document = load(valid_root / "art/world.json")
    for name in QUERY_FIXTURE_ROOTS - {"typed-query-fixture"}:
        folder = root / name
        require(all((folder / filename).read_bytes() == (valid_root / filename).read_bytes()
                    for filename in QUERY_FIXTURE_FILES - {"art/world.json", "manifest.json"}),
                "Rejected catalog changed a fixture payload outside the declared descriptor mutation")
        candidate = copy.deepcopy(load(folder / "art/world.json"))
        if name == "typed-query-invalid-size":
            require(candidate["lit_sprites"][0]["source_size"][0] == 0, "Invalid source-size control differs")
            candidate["lit_sprites"][0]["source_size"][0] = document["lit_sprites"][0]["source_size"][0]
        elif name == "typed-query-invalid-rect":
            require(candidate["materials"][0]["channels"]["albedo"]["rect"][0] == 5.5,
                    "Fractional material-rectangle control differs")
            candidate["materials"][0]["channels"]["albedo"]["rect"][0] = 5
        elif name == "typed-query-invalid-hash":
            require(candidate["textures"][0]["sha256"] == "0" * 64, "Invalid texture-hash control differs")
            candidate["textures"][0]["sha256"] = document["textures"][0]["sha256"]
        else:
            aliases = candidate["lit_sprites"]
            require(len(aliases) == 684 and len({sprite["id"] for sprite in aliases}) == 684 and
                    [sprite["id"] for sprite in aliases] == ["query.typed.sprite"] +
                    [f"query.typed.sprite.{index}" for index in range(1,684)], "Actual mask-budget alias inputs differ")
            for sprite in aliases:
                normalized = dict(sprite, id="query.typed.sprite")
                require(normalized == document["lit_sprites"][0], "Mask-budget alias changed source registration")
            candidate["lit_sprites"] = document["lit_sprites"]
        require(candidate == document, "Invalid catalog differs beyond its declared candidate mutation")
    raw = (valid_root / "art/material/albedo.rtex").read_bytes()
    expected_raw = bytearray()
    for y in range(149):
        for x in range(205):
            alpha = 255
            if 5 <= x < 198 and 7 <= y < 136:
                sx, sy = x - 5, y - 7
                alpha = 0 if sx < 48 else 63 if sx < 96 else 64 if sx < 144 else 255
                if 112 <= sx <= 131 and 48 <= sy <= 79:
                    alpha = 0
                if sx == 191:
                    alpha = 0
                elif sx == 192:
                    alpha = 64
            expected_raw.extend((170,130,90,alpha))
    require(raw == expected_raw, "Authored query fixture alpha threshold/hole/atlas gutter pattern differs")
    with Image.open(valid_root / "source.png") as image:
        require(image.size == (205,149) and image.convert("RGBA").tobytes() == raw,
                "Legacy fixture PNG and typed source albedo differ")
    fixture = value["fixture"]
    require(fixture["alpha_controls"] == {"zero_clear": True, "63_clear": True, "64_covered": True,
            "255_covered": True, "hole_clear": True, "hole_neighbor_covered": True,
            "opaque_padding_excluded": True, "pooled_pair_covered": True} and
            fixture["quantized_zero_opacities"] == [0.0,struct.unpack("<f",struct.pack("<f",.001))[0]] and
            fixture["budget_candidate_typed_ids"] == 684 and
            fixture["budget_candidate_requested_bytes"] == (684 + 1) * 192 * 128 > 16777216,
            "Typed alpha/pooled-pair/zero-opacity/budget controls differ")
    coverage = verify_query_row(valid_root, document, fixture["row"])
    require(fixture["row"]["id"] == "query.typed.sprite" and fixture["row"]["crop"] == [5,7,193,129] and
            fixture["row"]["atlas_size"] == [205,149] and fixture["row"]["pivot_px"] == [47.25,118.5] and
            fixture["row"]["export_scale"] == 2.25 and fixture["row"]["mask_size"] == [192,128],
            "Independent subrect/noncentral pivot fixture differs")
    caches = {"ready": True, "hit_mask_count": 2, "hit_mask_bytes": 49152, "outline_count": 1,
              "outline_bytes": 98304, "legacy_contains": True, "typed_contains": True}
    require(fixture["caches"] == caches and fixture["cold_reload"] ==
            {"succeeded": True, "queries_same": True, "caches_same": True}, "Cold reload or bounded cache preservation differs")
    startup = fixture["startup_reload"]
    require(startup["rejected"] is True and startup["unchanged"] is True and
            "startup-only" in startup["error"], "Typed schema reload after actual UI submission was not rejected")
    legacy = fixture["legacy"]
    mirrored = b"".join(coverage[offset:offset + 192][::-1] for offset in range(0, len(coverage), 192))
    require(legacy["id"] == "building.query.reference" and legacy["coverage_sha256"] == hashlib.sha256(coverage).hexdigest() and
            legacy["mirrored_sha256"] == hashlib.sha256(mirrored).hexdigest() and legacy["covered"] == coverage.count(255) and
            legacy["clear"] == coverage.count(0) and legacy["outline_quads"] == 8 and
            all(legacy[key] is True for key in ("bounds_equivalent", "picking_equivalent", "mirror_equivalent", "outline_geometry")),
            "Legacy alpha/mirror/outline equivalence differs")
    require(len(legacy["widths"]) == len(legacy["bounds"]) == 3, "Legacy transformed registration controls differ")
    for width, bounds, typed in zip(legacy["widths"], legacy["bounds"], fixture["row"]["transforms"], strict=True):
        require(abs(width - typed["bounds"][2]) <= 0.0001 and len(bounds) == 4 and
                all(abs(a - b) <= 0.0001 for a,b in zip(bounds,typed["bounds"],strict=True)),
                "Legacy registration differs from typed source pivot/extent")
    reasons = {"Q-catalog-size": ("typed-query-invalid-size", "source"),
               "Q-catalog-rect": ("typed-query-invalid-rect", "rect"),
               "Q-catalog-hash": ("typed-query-invalid-hash", "hash"),
               "Q-catalog-budget": ("typed-query-invalid-budget", "budget")}
    rejected = value["catalog_rejections"]
    require_ids(rejected, set(reasons), ("rejected", "unchanged"))
    for row in rejected:
        expected_root, reason = reasons[row["id"]]
        require(row["root"] == expected_root and isinstance(row["error"],str) and reason in row["error"].lower() and
                row["caches_before"] == row["caches_after"] == caches, "Rejected candidate altered accepted query/cache state")


def verify_semantics(root: Path, native: dict) -> None:
    from PIL import Image
    require(set(native) == {"schema", "complete", "deterministic", "measured"}, "Native UI root schema differs")
    value = native["deterministic"]
    require(set(value) == {"controls", "invalid_controls", "captures", "world_saves", "capture_limit", "backend",
        "direct", "ui", "interleave", "frames_in_flight", "resize", "carrier", "final_preservation", "scope", "open", "hardware", "typed_queries"},
        "Actual UI semantic schema differs")
    require(value["backend"] == "direct3d12" and value["world_saves"] == ["ui-world-current.save"] and value["capture_limit"] == 15 and
            value["scope"] == "Actual single RmlUi lifecycle; direct UI Legacy/cumulative oracle; diagnostic typed World" and
            set(value["open"]) == OPEN_SCOPE and len(value["open"]) == 6, "Actual UI scope overstated")
    hardware = value["hardware"]
    require(set(hardware) == {"backend", "gpu", "driver", "software", "resolution", "graph", "fps", "gpu_execution_ms"} and
            hardware["backend"] == "direct3d12" and hardware["software"] is False and hardware["resolution"] == [1920,1080] and
            hardware["graph"] == "WaterAtmosphereSlice" and hardware["fps"] is None and hardware["gpu_execution_ms"] is None and
            isinstance(hardware["gpu"],str) and hardware["gpu"] and isinstance(hardware["driver"],str) and hardware["driver"],
            "Actual UI hardware binding or performance scope differs")
    require_ids(value["controls"], POSITIVE_IDS, ("passed",))
    require_ids(value["invalid_controls"], INVALID_IDS,
                ("rejected", "targets_unchanged", "completed_submission_unchanged", "next_valid_rendered"))
    require(all(isinstance(row["error"], str) and row["error"] for row in value["invalid_controls"]), "Invalid control exception absent")
    ui = value["ui"]
    require(ui["locale"] == "uk", "Actual UI locale differs")
    require_ids(ui["prefix_invalid_controls"], PREFIX_IDS, ("rejected", "unchanged", "next_valid_rendered"))
    images = {}
    captures = value["captures"]
    require(len(captures) == 15 and {row["file"] for row in captures} == CAPTURES, "Actual UI capture inventory differs")
    world_names = {"ui-world-before.png", "ui-rml-hud.png", "ui-world-after.png", "ui-invalid-recovered.png"}
    zero_counters = ("surface_draws", "ground_draws", "material_draws", "occluders", "occluder_upload_bytes",
        "contact_casters", "contact_draws", "contact_mask_binds", "water_draws", "reflection_draws")
    for row in captures:
        expected_extent = (1601,901) if row["file"] == "ui-odd-resize.png" else (1920,1080)
        require((row["width"],row["height"]) == expected_extent and (root / row["file"]).stat().st_size == row["bytes"],
                "UI capture extent or byte count differs")
        with Image.open(root / row["file"]) as picture:
            rgba = picture.convert("RGBA")
            require(rgba.size == expected_extent, "Decoded UI capture extent differs")
            raw = rgba.tobytes(); require(set(raw[3::4]) == {255}, "UI final alpha differs")
            images[row["file"]] = raw
        telemetry = row["telemetry"]
        require(telemetry["submitted"] is True and telemetry["capture"] is True and telemetry["gpu_execution_ms"] is None,
                "UI actual submission/capture telemetry differs")
        if row["file"] in world_names:
            require(row["content"] == 0 and telemetry["graph_pass_count"] == 8 and
                    telemetry["graph_passes"] == [6,7,8,0,1,2,3,4] and telemetry["water_draws"] > 0 and
                    telemetry["reflection_draws"] == 2 and telemetry["contact_draws"] == 0, "Actual World graph differs")
        else:
            require(row["content"] == 1 and telemetry["graph_pass_count"] == 1 and telemetry["graph_passes"] == [4] and
                    all(telemetry[key] == 0 for key in zero_counters), "UI-only frame executed World passes")
    background = bytes((20,30,40,255))
    require(images["ui-empty.png"] == background * (1920*1080), "Empty UI clear differs")
    def changed(first: str, second: str) -> int:
        a,b = images[first],images[second]
        require(len(a) == len(b), "Pixel comparison extent differs")
        return sum(a[i:i+4] != b[i:i+4] for i in range(0,len(a),4))
    direct = images["ui-direct-cumulative.png"]
    require(all(direct == images[name] for name in ("ui-direct-legacy.png", "ui-flight-1.png", "ui-flight-2.png",
            "ui-flight-3.png", "ui-restored.png")), "Actual direct UI equivalent pixels differ")
    require(images["ui-world-before.png"] == images["ui-world-after.png"] == images["ui-invalid-recovered.png"],
            "Actual World recovered pixels differ")
    require(images["ui-rml-menu.png"] == images["ui-rml-menu-neutral.png"], "Optional UI companions changed pixels")
    def pixel(x: int, y: int) -> list[int]:
        offset=(y*1920+x)*4; return list(direct[offset:offset+4])
    expected = [74,47,36,255]; actual = pixel(180,150)
    text_count=sum(pixel(x,y) != list(background) for y in range(70,150) for x in range(390,700))
    require(value["direct"] == {"pixels_same": True, "premultiplied_expected": expected, "premultiplied_actual": actual,
        "scissor_outside_same": True, "text_changed_pixels": text_count,
        "changed_pixels": changed("ui-empty.png","ui-direct-cumulative.png")} and
        all(abs(a-b) <= 1 for a,b in zip(actual,expected,strict=True)) and text_count > 100 and
        all(pixel(x,y) == list(background) for x,y in ((120,150),(280,150),(180,115),(180,205))),
        "Actual premultiplied/scissor/text pixel oracle differs")
    for key in ("menu", "menu_neutral", "hud"):
        row=ui[key]
        require(row["vertices"] > 0 and row["indices"] > 0 and row["ui_draws"] > 0 and row["textured_draws"] > 0 and
                row["distinct_textures"] >= 2 and row["premultiplied_draws"] > 0, "Actual RmlUi geometry is vacuous")
    require(ui["menu"]["surface_vertices"] == 0 and ui["menu"]["changed_pixels"] ==
        changed("ui-empty.png","ui-rml-menu.png") > 10000 and ui["menu_neutral"]["surface_vertices"] ==
        ui["menu_neutral"]["vertices"] and ui["menu_neutral"]["pixels_same"] is True, "Actual menu companions/pixels differ")
    hud=ui["hud"]
    require(hud["surface_vertices"] == hud["vertices"] and hud["vertices"] > hud["prefix_vertices"] > 0 and
            hud["indices"] > hud["prefix_indices"] > 0 and hud["commands"] > hud["prefix_commands"] > 0 and
            all(hud[key] is True for key in ("prefix_preserved", "neutral_suffix", "carrier_same")) and
            hud["changed_pixels"] == changed("ui-world-before.png","ui-rml-hud.png") > 1000, "Actual HUD prefix/pixel witness differs")
    require(all(value["interleave"][key] is True for key in ("pixels_same", "carrier_same", "targets_unchanged")), "Interleave proof failed")
    flights=value["frames_in_flight"]
    require(len(flights)==3 and [row["requested"] for row in flights]==[1,2,3], "Actual presentation depth controls differ")
    for row in flights:
        require(type(row["supported"]) is bool and 1<=row["effective"]<=3 and
                (not row["supported"] or row["effective"]==row["requested"]) and row["slots"]==list(range(row["effective"])) and
                row["actual_ui_submissions"]==row["effective"]*2+2 and
                all(row[key] is True for key in ("pixels_same","carrier_same","world_targets_unchanged")), "Actual UI ring-slot control differs")
    resize=value["resize"]
    for key,extent in (("odd",(1601,901)),("restored",(1920,1080))):
        row=resize[key]
        require((row["logical_width"],row["logical_height"])==extent and
                (row["drawable_width"],row["drawable_height"])==extent, "Actual resize dimensions differ")
    require(all(resize[key] is True for key in ("ui_targets_unchanged","ui_pixels_restored","world_carrier_restored")) and
        [resize["odd_world_targets"][key] for key in ("width","height")]==[1601,901] and
        [resize["restored_world_targets"][key] for key in ("width","height")]==[1920,1080], "Actual World target resize failed")
    require(value["final_preservation"] == {"settings_absent":True,"settings_actions":0,"fixture_bytes_unchanged":True,
            "fixture_bak_absent":True,"world_pixels_same":True,"carrier_same":True} and
            not (root/"ui-settings-unwritten.json").exists() and not (root/"ui-world-current.save.bak").exists(), "Actual settings/save preservation failed")
    carrier=(root/"ui-carrier.rgba8").read_bytes()
    tuples=Counter(zip(carrier[0::4],carrier[1::4],carrier[2::4],carrier[3::4])); counts=value["carrier"]
    water=sum(n for rgba,n in tuples.items() if rgba[0]==255 and rgba[1]==2 and rgba[2]%17==0 and rgba[3]==255)
    shore=sum(n for rgba,n in tuples.items() if rgba[0]==255 and rgba[1]==2 and rgba[2]%17==0 and rgba[2]>0 and rgba[3]==255)
    require(tuples[(0,0,0,0)]==counts["clear_pixels"]>0 and tuples[(0,1,0,0)]==counts["land_pixels"]>0 and
            tuples[(0,3,0,0)]==counts["bridge_deck_pixels"]>0 and water==counts["water_pixels"]>0 and
            shore==counts["shore_water_pixels"]>0 and counts["invalid_tuples"]==0 and
            sum(tuples.values())==tuples[(0,0,0,0)]+tuples[(0,1,0,0)]+tuples[(0,3,0,0)]+water and
            counts["packed_bytes"]==len(carrier)==1920*1080*4 and counts["width"]==1920 and counts["height"]==1080 and
            counts["gpu_row_pitch"]>=1920*4 and counts["gpu_row_pitch"]%256==0, "Actual raw carrier classification differs")


def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("build","kit","ui_kit","ui_repeat","first","second"):
        parser.add_argument(name,type=Path)
    args=parser.parse_args()
    results,counts=verify_bindings(args.build,args.kit,args.ui_kit,args.ui_repeat,args.first,args.second)
    world_witnesses = []
    for root,native in zip((args.first,args.second),results,strict=True):
        verify_semantics(root,native)
        verify_typed_queries(root,args.kit,native["deterministic"]["typed_queries"])
        world_witnesses.append(verify_visible_world(root))
    require(world_witnesses[0] == world_witnesses[1], "Repeated nonvacuous World witnesses differ")
    print("UI GameView evidence checks passed; " + ", ".join(f"{key}={value}" for key,value in counts.items()))


if __name__=="__main__":
    main()
