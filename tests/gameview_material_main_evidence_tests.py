"""Read-only evidence for ordinary main startup captures, not material/FPS acceptance."""
from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
import math
import os
import re
import stat
from collections import Counter
from pathlib import Path

REPOSITORY = Path(r"C:\Users\fipst\Desktop\ribterra")
MAIN_SHA = "7a7018a3e62d1076c662e166ef12228ed5eefadacca3bf74acb5ed76bc2e4439"
HELPER_SHA = "a6a289415d8457c978c0b15ebc05fd60c377e568128e2edc213a4fdf6072641d"
CASES = ("main-menu", "main-material-menu", "main-world", "main-material-world")
LIMIT = 128 * 1024 * 1024
HEADROOM = 384 * 1024 * 1024
# Conservative central region inside the source-qualified HUD-free rectangle.
SCENE_CROP = (320, 200, 1080, 650)
PRODUCTION = {
    "foundation": "src/foundation/foundation.cpp", "economy": "src/foundation/economy.cpp",
    "lobby": "src/foundation/lobby.cpp", "atomic_file": "src/foundation/atomic_file.cpp",
    "platform": "src/foundation/platform.cpp", "world": "src/sim/world.cpp", "weather": "src/sim/weather.cpp",
    "skills": "src/sim/skills.cpp", "world_save": "src/sim/world_save.cpp", "game_server": "src/server/game_server.cpp",
    "gns_transport": "src/net/gns_transport.cpp", "protocol": "src/net/protocol.cpp",
    "network_session": "src/net/network_session.cpp", "asset_catalog": "src/assets/asset_catalog.cpp",
    "texture_atlas": "src/assets/texture_atlas.cpp", "localization": "src/ui/localization.cpp",
    "renderer2d": "src/render/renderer2d.cpp", "contact_shadow": "src/render/contact_shadow.cpp",
    "sky_shadow": "src/render/sky_shadow.cpp", "view": "src/client/view.cpp",
    "client_session": "src/client/client_session.cpp", "game_art": "src/client/game_art.cpp", "game_ui": "src/ui/game_ui.cpp",
}
BASE_FLAGS = ["/nologo", "/TP", "/DWIN32", "/D_WINDOWS", "/EHsc", "/O2", "/Ob2", "/DNDEBUG", "/std:c++20",
              "/MT", "/utf-8", "/W4", "/WX", "/permissive-", "/GR", "/Zc:__cplusplus", "/DRMLUI_STATIC_LIB",
              '/DRIBTERRA_PROBE_BUILD="gameview-material-isolated-unversioned"']
ENVIRONMENT = {"include", "lib", "vs_install_dir", "vc_tools_version", "windows_sdk_dir", "windows_sdk_version"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def same(first: str | Path, second: str | Path) -> bool:
    return os.path.normcase(os.path.abspath(first)) == os.path.normcase(os.path.abspath(second))


def no_reparse(path: Path) -> None:
    require(path.is_absolute(), f"Non-absolute path: {path}")
    for item in (path, *path.parents):
        if item.exists() or item.is_symlink():
            attrs = item.lstat()
            require(not item.is_symlink() and not (getattr(attrs, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT),
                    f"Reparse path: {item}")


def inside(path: Path, root: Path) -> None:
    no_reparse(path)
    require(os.path.commonpath((os.path.normcase(str(path)), os.path.normcase(str(root)))) == os.path.normcase(str(root))
            and not same(path, root), f"File escapes root: {path}")


def digest(path: Path) -> str:
    no_reparse(path)
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load(path: Path) -> object:
    no_reparse(path)
    return json.loads(path.read_text(encoding="utf-8-sig"))


def hash_row(row: dict, paths: tuple[str, ...] = ("path",)) -> None:
    require(type(row.get("bytes")) is int and row["bytes"] >= 0 and
            isinstance(row.get("sha256"), str) and re.fullmatch("[0-9a-fA-F]{64}", row["sha256"]) is not None,
            "Invalid byte/hash row")
    if "unchanged" in row:
        require(row["unchanged"] is True, "Recorded binding changed")
    if "after_bytes" in row:
        require(row["after_bytes"] == row["bytes"], "Recorded final byte size changed")
    if "after_sha256" in row:
        require(row["after_sha256"].lower() == row["sha256"].lower(), "Recorded final hash changed")
    for name in paths:
        path = Path(row[name]); no_reparse(path)
        require(path.is_file() and path.stat().st_size == row["bytes"] and digest(path) == row["sha256"].lower(),
                f"Bound file changed: {path}")


def external_headers(report: dict) -> dict:
    require(report.get("external_header_tree_format") == "root-relative-sha256-base64-triplets-v1",
            "Unknown or missing external SDK header format")
    encoded = report.get("external_header_trees")
    require(isinstance(encoded, dict), "External SDK observations missing")
    result = {}
    for side in ("trees_before", "trees_after"):
        trees = encoded.get(side)
        require(isinstance(trees, list) and len(trees) == 4, "Four external SDK trees required per observation")
        decoded = []
        for tree_row in trees:
            require(isinstance(tree_row, dict) and isinstance(tree_row.get("root"), str) and
                    isinstance(tree_row.get("files"), list), "Invalid external SDK tree")
            root = Path(tree_row["root"]); no_reparse(root)
            rows, seen = [], set()
            for item in tree_row["files"]:
                require(isinstance(item, list) and len(item) == 3 and isinstance(item[0], str) and
                        type(item[1]) is int and 0 <= item[1] <= (1 << 63) - 1 and isinstance(item[2], str),
                        "SDK tuple must contain relative path, integer byte size and base64 SHA256")
                relative, size, spelling = item
                require(relative and "\\" not in relative and ":" not in relative and not Path(relative).is_absolute() and
                        all(part not in ("", ".", "..") for part in relative.split("/")) and
                        os.path.normcase(relative) not in seen, "Unsafe or duplicate SDK relative path")
                seen.add(os.path.normcase(relative))
                path = root / relative; inside(path, root)
                require(path.relative_to(root).as_posix() == relative, "Noncanonical SDK relative path")
                require(re.fullmatch(r"[A-Za-z0-9+/]{43}=", spelling) is not None, "Invalid SDK base64 SHA256 spelling")
                try:
                    raw = base64.b64decode(spelling, validate=True)
                except binascii.Error as error:
                    raise AssertionError("Invalid SDK SHA256 bytes") from error
                require(len(raw) == 32 and base64.b64encode(raw).decode("ascii") == spelling, "Noncanonical SDK SHA256 bytes")
                rows.append({"relative_path": relative, "path": str(path), "bytes": size, "sha256": raw.hex()})
            decoded.append({"root": tree_row["root"], "files": rows})
        result[side] = decoded
    return result


def bindings(value: object) -> set[str]:
    result: set[str] = set()
    if isinstance(value, dict):
        if "external_header_trees" in value or "external_header_tree_format" in value:
            result.update(bindings(external_headers(value)))
        if "bytes" in value and "sha256" in value:
            names = tuple(k for k in ("path", "repository_path", "snapshot_path", "origin", "destination") if k in value)
            hash_row(value, names)
            result.update(os.path.normcase(os.path.abspath(value[k])) for k in names)
        for child in value.values():
            result.update(bindings(child))
    elif isinstance(value, list):
        for child in value:
            result.update(bindings(child))
    return result


def files(root: Path) -> dict[str, Path]:
    no_reparse(root); require(root.is_dir(), f"Missing input tree: {root}")
    result: dict[str, Path] = {}
    for folder, directories, names in os.walk(root, followlinks=False):
        for name in directories:
            no_reparse(Path(folder) / name)
        for name in names:
            path = Path(folder) / name; no_reparse(path)
            result[path.relative_to(root).as_posix()] = path
    return result


def tree(root: Path, rows: list[dict]) -> None:
    current = files(root)
    require(len(rows) == len({r["relative_path"] for r in rows}) == len(current), f"Tree membership differs: {root}")
    require({r["relative_path"] for r in rows} == set(current), f"Tree filenames differ: {root}")
    for row in rows:
        path = Path(row["path"]); inside(path, root)
        require(same(path, current[row["relative_path"]]), "Tree row path differs")
        hash_row(row)


def command(row: dict, executable: Path, argv: list[str], cwd: Path, name: str, evidence_root: Path | None = None) -> None:
    evidence_root = cwd if evidence_root is None else evidence_root
    require(same(row["executable"], executable) and row["arguments"] == argv and same(row["working_directory"], cwd) and
            type(row["exit_code"]) is int and row["exit_code"] == 0, f"Actual command differs: {name}")
    for key, suffix in (("log", ".log"), ("exit_sidecar", ".log.exit.txt")):
        require(same(row[key]["path"], evidence_root / (name + suffix)), "Actual command evidence filename differs")
        hash_row(row[key])
    require(Path(row["exit_sidecar"]["path"]).read_text(encoding="utf-8-sig").strip() == "0", "Actual exit sidecar is nonzero")


def main_build(root: Path, material: Path) -> tuple[dict, dict, set[str]]:
    require(same(root.parent, REPOSITORY / "build") and same(material.parent, REPOSITORY / "build") and not same(root, material),
            "Main/material roots must be distinct immediate repository build children")
    report_path = root / "main-build-report.json"; report = load(report_path)
    require(report["complete"] is True and same(report["authoritative_repository"], REPOSITORY) and same(report["output_root"], root),
            "Main build is incomplete or misplaced")
    require(report["compiled_translation_unit_count"] == 1 and report["reused_production_object_count"] == 23 and
            report["shader_file_count"] == 93 and report["shader_compilation"] is False and
            report["execution"] == {"game": False, "gpu": False}, "Main build scope differs")
    accepted_root = Path(report["accepted_build_root"])
    require(same(accepted_root.parent, REPOSITORY / "build"), "Accepted production build root escapes build")
    require(same(report["accepted_build_report"]["path"], accepted_root / "build-report.json"), "Production report path differs")
    accepted = load(accepted_root / "build-report.json")
    require(accepted["build_label"] == "gameview-material-isolated-unversioned" and accepted["translation_unit_count"] == 24 and
            accepted["internal_production_translation_unit_count"] == 23 and same(accepted["output_root"], accepted_root) and
            same(accepted["authoritative_repository"], REPOSITORY), "Production build identity differs")
    require(accepted["translation_units"] == [*PRODUCTION.values(), "tools/gameview_material_probe.cpp"], "Production TU closure differs")
    external = [REPOSITORY / ".deps/SDL3-3.4.16/include", REPOSITORY / ".deps/SDL3_ttf-3.2.2/include",
                REPOSITORY / "vcpkg_installed/x64-windows-release/include/GameNetworkingSockets",
                REPOSITORY / "vcpkg_installed/x64-windows-release/include"]
    flags = BASE_FLAGS + ["/I" + str(accepted_root / "source/include"), "/I" + str(accepted_root / "generated")] + \
        ["/external:I" + str(path) for path in external] + ["/external:W0"]
    require(report["flags"] == accepted["flags"] == flags and report["compiler_environment"] == accepted["compiler_environment"] and
            set(report["compiler_environment"]) == ENVIRONMENT and all(report["compiler_environment"].values()), "Strict compile environment differs")
    require(len(accepted["commands"]) == 122 and all(c["exit_code"] == 0 for c in accepted["commands"]), "Production actual command ledger differs")
    tools = {Path(r["path"]).name: r for r in accepted["tools"]}
    require("cl.exe" in tools and "link.exe" in tools, "Actual compiler/linker identities missing")
    for key in ("compiler", "linker"):
        require(report["toolchain"][key] == tools["cl.exe" if key == "compiler" else "link.exe"], "Main actual tool differs from accepted objects")
    objects = [str(accepted_root / f"obj/{name}.obj") for name in PRODUCTION]
    require([r["path"] for r in accepted["objects"]] == [*objects, str(accepted_root / "obj/gameview_material_probe.obj")], "Production object order differs")
    require([r["path"] for r in report["reused_objects"]] == objects and
            same(report["excluded_object"], accepted_root / "obj/gameview_material_probe.obj") and
            same(report["compiled_object"]["path"], root / "obj/main.obj"), "Reused/compiled object closure differs")
    require(report["link_libraries"] == accepted["link_libraries"] and
            len({r["path"] for r in accepted["link_libraries"]}) == len(accepted["link_libraries"]) > 0, "Link libraries differ")
    by_log = {Path(c["log"]["path"]).name: c for c in accepted["commands"]}
    for name, relative in PRODUCTION.items():
        extra = ["/DSTEAMNETWORKINGSOCKETS_STATIC_LINK"] if name == "gns_transport" else []
        command(by_log[f"compile-{name}.log"], Path(tools["cl.exe"]["path"]),
                flags + extra + ["/c", str(accepted_root / "source" / relative), "/Fo" + str(accepted_root / f"obj/{name}.obj")],
                REPOSITORY, "compile-" + name, accepted_root)
    libraries = [r["path"] for r in accepted["link_libraries"]]
    link_prefix = ["/nologo", "/machine:x64", "/incremental:no", "/debug:none", "/subsystem:console"]
    command(by_log["link-GameViewMaterialProbe.log"], Path(tools["link.exe"]["path"]),
            link_prefix + ["/out:" + str(accepted_root / "GameViewMaterialProbe.exe"), *objects,
                           str(accepted_root / "obj/gameview_material_probe.obj"), *libraries],
            REPOSITORY, "link-GameViewMaterialProbe", accepted_root)
    require(len(report["commands"]) == 2, "Main must have exactly compile/link commands")
    command(report["commands"][0], Path(tools["cl.exe"]["path"]),
            flags + ["/c", str(root / "source/src/client/main.cpp"), "/Fo" + str(root / "obj/main.obj")], root, "compile-main")
    command(report["commands"][1], Path(tools["link.exe"]["path"]),
            link_prefix + ["/out:" + str(root / "Ribterra.exe"), str(root / "obj/main.obj"), *objects, *libraries], root, "link-Ribterra")
    require(same(report["main_source"]["path"], REPOSITORY / "src/client/main.cpp") and report["main_source"]["sha256"].lower() == MAIN_SHA and
            same(report["executable"]["path"], root / "Ribterra.exe"), "Actual main/EXE identity differs")
    source = report["source_provenance"]
    require(len(source) == 2 and {Path(r["origin"]).name for r in source} == {"main.cpp", "build_gameview_material_main_isolated.ps1"},
            "Main/helper snapshots differ")
    helper = next(r for r in source if Path(r["origin"]).name.endswith(".ps1"))
    require(helper["sha256"].lower() == HELPER_SHA and same(helper["origin"], REPOSITORY / "tools/build_gameview_material_main_isolated.ps1"),
            "Main helper is not the independently reviewed source")
    immutable = load(Path(report["immutable_input_bindings"]["path"]))
    final = load(Path(report["immutable_input_final_binding"]["path"]))
    require({r["path"] for r in immutable} == {r["path"] for r in final}, "Main input before/after sets differ")
    required = bindings(report) | bindings(accepted) | bindings(immutable) | bindings(final)
    source_rows, final_rows = accepted["source_provenance"], accepted["source_final_binding"]
    require(len(source_rows) == len(final_rows) > 0 and
            len({r["relative_path"] for r in source_rows}) == len(source_rows) and
            {r["relative_path"] for r in source_rows} == {r["relative_path"] for r in final_rows},
            "Production final source cohort differs")
    final_by_relative = {r["relative_path"]: r for r in final_rows}
    snapshot_rows = []
    for r in source_rows:
        require(same(r["repository_path"], REPOSITORY / r["relative_path"]) and
                same(r["snapshot_path"], accepted_root / "source" / r["relative_path"]), "Production source path differs")
        final_row = final_by_relative[r["relative_path"]]
        require(final_row["unchanged"] is True and same(final_row["repository_path"], r["repository_path"]) and
                same(final_row["snapshot_path"], r["snapshot_path"]) and final_row["bytes"] == r["bytes"] and
                final_row["sha256"].lower() == r["sha256"].lower(), "Production final source binding differs")
        snapshot_rows.append({**r, "path": r["snapshot_path"]})
    tree(accepted_root / "source", snapshot_rows)
    headers = external_headers(accepted)
    trees_before, trees_after = headers["trees_before"], headers["trees_after"]
    require(len(trees_before) == len(trees_after) == 4, "Four explicit external include trees required")
    for before, after, path in zip(trees_before, trees_after, external, strict=True):
        require(same(before["root"], path) and same(after["root"], path) and before["files"] == after["files"], "External SDK membership changed")
        tree(path, before["files"])
    generated = {"network_frame_generated.h", "network_generated.h", "skill_balance_generated.h", "world_save_generated.h"}
    require({Path(r["path"]).name for r in accepted["generated_headers"]} == generated and
            set(files(accepted_root / "generated")) == generated, "Generated header closure differs")
    for name in ("control_kit", "ui_kit", "original_art", "material_collection"):
        tree(Path(accepted[name]["root"]), accepted[name]["files"])
    require(same(accepted["original_art"]["root"], REPOSITORY / "assets") and len(accepted["original_art"]["files"]) == 181 and
            same(accepted["material_collection"]["root"], material) and len(accepted["material_collection"]["files"]) == 18,
            "Original181/material18 distinct file closure differs")
    # Notices may already be manifest references: compare unique paths, never double-count NOTICE.txt.
    material_manifest = load(material / "manifest.json")
    material_paths = {r["path"] for r in material_manifest["assets"]} | {r["notice"] for r in material_manifest["assets"]} | \
        {"manifest.json", "material-collection-report.json", "NOTICE.txt"}
    require(material_paths == set(files(material)), "Material unique manifest/notice/report closure differs")
    require(accepted["shader_compilation"]["fresh_shader_compilation"] is True and
            accepted["shader_compilation"]["program_count"] == 23 and accepted["shader_compilation"]["stage_count"] == 46 and
            accepted["shader_compilation"]["total_file_count"] == 93, "Fresh shader compile scope differs")
    require(len(report["shader_files"]) == 93 and len(files(root / "assets/shaders")) == 93 and
            all(Path(r["destination"]).parent == root / "assets/shaders" and
                same(r["origin"], accepted_root / "assets/shaders" / Path(r["destination"]).name) for r in report["shader_files"]),
            "Main shader copies differ")
    runtime = ("SDL3.dll", "SDL3_ttf.dll", "assets/fonts/NotoSans-Regular.ttf")
    require(len(report["runtime_files"]) == 3 and all(same(r["destination"], root / name) and same(r["origin"], accepted_root / name)
                for r, name in zip(report["runtime_files"], runtime, strict=True)), "Main runtime DLL/font closure differs")
    tree(accepted_root, load(Path(report["accepted_build_tree"]["path"])))
    root_rows = [*report["output_files_before_report"], {"relative_path": "main-build-report.json", "path": str(report_path),
                 "bytes": report_path.stat().st_size, "sha256": digest(report_path)}]
    tree(root, root_rows)
    preservation = load(Path(report["preservation_after"]["path"]))
    require(preservation["complete"] is True and preservation["errors"] == [] and preservation["accepted_build_tree_unchanged"] is True and
            preservation["external_headers_and_asset_membership_checked"] is True, "Main build preservation failed")
    require(sum(p.stat().st_size for p in files(root).values()) <= LIMIT, "Main build exceeds128MiB")
    required.add(os.path.normcase(str(report_path))); required.add(os.path.normcase(str(accepted_root / "build-report.json")))
    return report, accepted, required


def pixels(path: Path) -> tuple[object, dict]:
    from PIL import Image
    with Image.open(path) as image:
        require(image.format == "PNG" and image.size == (1440, 900), "Actual main capture dimensions/format differ")
        rgba = image.convert("RGBA"); rgba.load()
    require(rgba.getextrema()[3] == (255, 255), "Main capture is not opaque")
    colors = rgba.getcolors(4096)
    nonblack = sum(pixel[:3] != (0, 0, 0) for pixel in rgba.getdata())
    require(nonblack > 1000 and (colors is None or len(colors) > 16), "Main capture is visually vacuous")
    raw = rgba.tobytes()
    return rgba, {"rgba_sha256": hashlib.sha256(raw).hexdigest(), "nonblack_rgb_pixels": nonblack,
                  "unique_rgba_up_to4096": None if colors is None else len(colors), "opaque_pixels": 1440 * 900}


def changed(first: object, second: object) -> int:
    from PIL import ImageChops
    # Compare RGB: RGBA getbbox can hide RGB changes when the difference alpha is0.
    difference = ImageChops.difference(first.convert("RGB"), second.convert("RGB"))
    return sum(pixel != (0, 0, 0) for pixel in difference.getdata())


def qualify_scene_crop(accepted_root: Path) -> dict:
    renderer = (accepted_root / "source/src/render/renderer2d.cpp").read_text(encoding="utf-8")
    ui = (accepted_root / "source/src/ui/game_ui.cpp").read_text(encoding="utf-8")
    css = (REPOSITORY / "assets/ui/hud-v01.rcss").read_text(encoding="utf-8")
    require("constexpr std::uint32_t logical_height = 900;" in renderer and
            "return {std::max(1440u, fitted_width), logical_height};" in renderer and
            "float scale{1};" in ui and "context->SetDensityIndependentPixelRatio(scale);" in ui,
            "Compiled logical extent/default UI density contract differs")

    def value(selector: str, property_name: str) -> float:
        blocks = re.findall(r"\.hud-v01 #" + re.escape(selector) + r"\s*\{([^}]+)\}", css)
        pattern = r"(?:^|;)\s*" + re.escape(property_name) + r"\s*:\s*([0-9]+(?:\.[0-9]+)?)(?:dp|px)\s*;"
        # Shared panel-decoration rules precede the controls' actual geometry.
        property_value = next((match for block in blocks if (match := re.search(pattern, block)) is not None), None)
        require(property_value is not None, "HUD source extent missing: " + selector + "/" + property_name)
        return float(property_value[1])

    # Fresh automatic profiles have no settings, selections, open panels or input actions.
    # At1440px the1250/1120px media overrides are inactive and density is1.
    # Top resource/calendar bounds end at88; warning begins at1104. The
    # minimap starts at1124/y696 and bottom actions begin at790. The chosen
    # crop leaves margins on each edge and also ends below any top warning.
    top_end = max(value(s, "top") + value(s, "height") for s in ("topbar", "layerbar"))
    warning_left = 1440 - value("food-warning", "right") - value("food-warning", "width")
    minimap_left = 1440 - value("controls", "right") - value("controls", "width")
    minimap_top = 900 - value("controls", "bottom") - value("controls", "height")
    actions_top = 900 - value("action-strip", "bottom") - value("action-strip", "height")
    require(SCENE_CROP[1] > top_end and SCENE_CROP[2] < min(warning_left, minimap_left) and
            SCENE_CROP[3] < min(minimap_top, actions_top), "Central scene crop intersects ordinary HUD source bounds")
    return {"crop": list(SCENE_CROP), "logical_size": [1440, 900], "ui_density": 1,
            "top_panels_end": top_end, "warning_left": warning_left, "minimap_left": minimap_left,
            "minimap_top": minimap_top, "actions_top": actions_top,
            "scope": "fresh automatic profile, no selection/input/open panels; original bound HUD styles"}


def scene_pixels(image: object) -> dict:
    region = image.crop(SCENE_CROP).convert("RGBA")
    counts = Counter(region.getdata())
    pixel_count = region.width * region.height
    modal_count = max(counts.values())
    nonmodal = pixel_count - modal_count
    require(len(counts) > 64 and nonmodal > 10000, "Central World scene is uniform or visually vacuous")
    return {"crop": list(SCENE_CROP), "pixel_count": pixel_count, "unique_rgba": len(counts),
            "modal_rgba": list(min(color for color, count in counts.items() if count == modal_count)),
            "modal_pixel_count": modal_count, "nonmodal_pixels": nonmodal,
            "rgba_sha256": hashlib.sha256(region.tobytes()).hexdigest()}


def scene_negative_controls() -> list[dict]:
    from PIL import Image
    result = []
    for name, color in (("uniform-black", (0, 0, 0, 255)), ("uniform-grey", (96, 96, 96, 255))):
        image = Image.new("RGBA", (1440, 900), color)
        try:
            scene_pixels(image)
        except AssertionError:
            result.append({"id": name, "rejected": True, "scope": "in-memory scene-pixel oracle; no GPU execution"})
        else:
            raise AssertionError("Central scene oracle accepted " + name)
    return result


def native(root: Path, main_root: Path, material: Path, report: dict, accepted: dict, required: set[str]) -> dict:
    require(same(root.parent, REPOSITORY / "build") and not same(root, main_root) and not same(root, material), "Native root overlaps inputs")
    ledger_path = root / "main-native-execution.json"; ledger = load(ledger_path)
    require(ledger["schema"] == 1 and ledger["complete"] is True and ledger["accepted"] is True and ledger["integrity_errors"] == [] and
            same(ledger["repository"], REPOSITORY) and same(ledger["main_build_root"], main_root) and
            same(ledger["accepted_build_root"], report["accepted_build_root"]) and same(ledger["material_root"], material) and
            same(ledger["output_root"], root), "Actual native ledger failed or root identity differs")
    require(ledger["process_count"] == 4 and [c["id"] for c in ledger["commands"]] == list(CASES), "Four ordinary processes not proven")
    require(ledger["automatic_contract"] == {"window": [1440, 900], "capture_frame_index": 2, "exit_after_frames": 3,
            "seed": 70421, "demo_preadvances": 1700,
            "profiles": "capture filename + .test-profile- + secure_token(16), retained empty climate-v1",
            "user_profile_scope": "Windows APPDATA/Ribterra/Ribterra hash/membership before/after; no contents retained"},
            "Source-defined automatic capture contract differs")
    bindings(ledger)
    before = load(Path(ledger["input_bindings"]["path"])); after = load(Path(ledger["input_final_binding"]["path"]))
    require(len(before) == len({r["path"] for r in before}) and {r["path"] for r in before} == {r["path"] for r in after},
            "Native input before/after membership differs")
    observed = bindings(before) | bindings(after)
    require(required <= observed, "Native execution omitted a strict build input binding")
    protected = load(Path(ledger["input_trees"]["path"]))
    require(len({os.path.normcase(t["root"]) for t in protected}) == len(protected), "Duplicate protected tree identity")
    expected_roots = {str(main_root), report["accepted_build_root"], *[accepted[k]["root"] for k in
                      ("control_kit", "ui_kit", "original_art", "material_collection")],
                      *[t["root"] for t in accepted["external_header_trees"]["trees_before"]]}
    require({os.path.normcase(t["root"]) for t in protected} == {os.path.normcase(p) for p in expected_roots}, "Protected tree closure differs")
    for value in protected:
        tree(Path(value["root"]), value["files"])
    preservation = load(Path(ledger["preservation"]["path"]))
    require(preservation["complete"] is True and preservation["errors"] == [] and preservation["protected_tree_count"] == len(protected),
            "Native preservation failed")
    prefs = preservation["ordinary_preferences"]
    require(same(prefs["root"], Path(os.environ["APPDATA"]) / "Ribterra/Ribterra") and
            type(prefs["existed_before"]) is bool and prefs["existed_before"] == prefs["existed_after"] and
            prefs["files_before"] == prefs["files_after"], "Ordinary user preferences changed")
    if prefs["existed_after"]:
        tree(Path(prefs["root"]), prefs["files_after"])
    else:
        require(not Path(prefs["root"]).exists() and prefs["files_before"] == [], "Absent ordinary profile was created")
    require(len(ledger["source_provenance"]) == 2 and {Path(s["origin"]).name for s in ledger["source_provenance"]} ==
            {"run_gameview_material_main_native.ps1", "gameview_material_main_evidence_tests.py"},
            "Runner/verifier source snapshots missing")
    for snapshot in ledger["source_provenance"]:
        name = Path(snapshot["origin"]).name
        original = REPOSITORY / ("tools" if name.endswith(".ps1") else "tests") / name
        require(name in {"run_gameview_material_main_native.ps1", "gameview_material_main_evidence_tests.py"} and
                same(snapshot["origin"], original) and same(snapshot["destination"], root / "source" / name), "Native source snapshot path differs")
    profiles = []; images = {}; pixel_rows = {}; scene_rows = {}; renderer_rows = []
    expected_files = {"main-native-execution.json", "input-bindings.json", "input-final-hashes.json", "input-trees-before.json", "preservation-after.json",
                      "source/run_gameview_material_main_native.ps1", "source/gameview_material_main_evidence_tests.py"}
    for case in ledger["commands"]:
        name = case["id"]; capture = root / f"{name}.png"; info = root / f"{name}.renderer-info.json"
        argv = ["--catalog-assets", str(REPOSITORY / "assets"), "--renderer-info", str(info)]
        argv += ["--menu-capture", str(capture)] if name.endswith("menu") else ["--demo", "--capture", str(capture)]
        if name.startswith("main-material-"):
            argv += ["--material-collection", str(material)]
        require(case["arguments"] == argv and case["actual_argv"] == [str(main_root / "Ribterra.exe"), *argv] and
                same(case["executable"]["path"], main_root / "Ribterra.exe") and same(case["working_directory"], main_root) and
                case["actual_subprocess"] is True and type(case["process_id"]) is int and case["process_id"] > 0 and
                type(case["exit_code"]) is int and case["exit_code"] == 0 and case["timed_out"] is False and case["launch_error"] == "" and
                type(case["elapsed_milliseconds"]) in (float, int) and math.isfinite(case["elapsed_milliseconds"]) and
                case["elapsed_milliseconds"] >= 0, "Actual ordinary main command/exit differs")
        recorded = load(root / f"case-{name}.json")
        require(recorded == {key: value for key, value in case.items() if key not in {"capture", "renderer_info", "profile"}},
                "Retained actual command ledger differs")
        require(same(case["log"]["path"], root / f"{name}.log") and same(case["exit_sidecar"]["path"], root / f"{name}.log.exit.txt") and
                Path(case["exit_sidecar"]["path"]).read_text(encoding="utf-8").strip() == "0" and
                same(case["capture"]["path"], capture) and same(case["renderer_info"]["path"], info), "Case output filename/exit differs")
        profile = case["profile"]; profile_path = Path(profile["root"])
        require(same(profile_path.parent, root) and profile_path.name == profile["relative_path"] and
                re.fullmatch(re.escape(name) + r"\.png\.test-profile-[0-9a-f]{32}", profile_path.name) is not None and
                profile["files"] == [] and profile["directories"] == ["climate-v1"] and files(profile_path) == {} and
                {p.name for p in profile_path.iterdir()} == {"climate-v1"} and (profile_path / "climate-v1").is_dir(),
                "Automatic profile is not source-defined empty isolated storage")
        no_reparse(profile_path / "climate-v1"); require(list((profile_path / "climate-v1").iterdir()) == [], "Automatic profile contains unexpected directories")
        profiles.append(profile_path.name)
        images[name], pixel_rows[name] = pixels(capture)
        if name.endswith("world"):
            scene_rows[name] = scene_pixels(images[name])
        hardware = load(info)
        require(hardware["backend"] == "direct3d12" and hardware["software"] is False and hardware["drawable"] == [1440, 900] and
                isinstance(hardware["device"], str) and hardware["device"] and isinstance(hardware["driver"], str) and hardware["driver"] and
                type(hardware["refresh_hz"]) in (float, int) and math.isfinite(hardware["refresh_hz"]) and hardware["refresh_hz"] >= 0,
                "Actual renderer hardware/drawable differs")
        version_source = (Path(report["accepted_build_root"]) / "source/include/ribterra/foundation.hpp").read_text(encoding="utf-8")
        version = re.search(r'engine_version\s*=\s*"([^"]+)"', version_source)
        require(version is not None and hardware["engine_version"] == version[1] and hardware["window_title"] == "Ribterra " + version[1],
                "Actual executable version/title differs from compiled headers")
        presentation = hardware["presentation"]
        require(presentation["requested"] == {"mode": "vsync", "frame_limit": 0, "frames_in_flight": 2} and
                presentation["effective"] == presentation["requested"] and presentation["GPU_controls_available"] is True and
                type(presentation["immediate_supported"]) is bool and type(presentation["mailbox_supported"]) is bool and
                presentation["reason"] == 0 and presentation["error"] == "", "Actual ordinary presentation contract differs")
        renderer_rows.append(hardware)
        expected_files.update({f"{name}.png", f"{name}.renderer-info.json", f"{name}.log", f"{name}.log.exit.txt", f"case-{name}.json"})
    require(all(row == renderer_rows[0] for row in renderer_rows), "Hardware/presentation changed between ordinary cases")
    require({p.name for p in root.iterdir() if p.is_dir()} == {"source", *profiles} and
            {p.name for p in (root / "source").iterdir()} ==
            {"run_gameview_material_main_native.ps1", "gameview_material_main_evidence_tests.py"},
            "Unexpected output/profile/source directories")
    require(set(files(root)) == expected_files and len(expected_files) == 27, "Exact27 main native evidence files differ")
    tree(root, [*ledger["native_output_before_ledger"], {"path": str(ledger_path), "relative_path": ledger_path.name,
                "bytes": ledger_path.stat().st_size, "sha256": digest(ledger_path)}])
    total = sum(p.stat().st_size for p in files(root).values())
    require(total <= LIMIT and ledger["storage"] == {"maximum_output_bytes": LIMIT, "minimum_free_bytes": HEADROOM,
            "minimum_observed_free_bytes": ledger["storage"]["minimum_observed_free_bytes"], "bulk_art_copied": False, "failed_roots_retained": True} and
            type(ledger["storage"]["minimum_observed_free_bytes"]) is int and ledger["storage"]["minimum_observed_free_bytes"] >= HEADROOM,
            "Actual bounded native storage/headroom differs")
    claims = ledger["claims"]
    require(claims == {"ordinary_startup_captures": True, "material_draw_count": None, "fps": None, "gpu_execution_ms": None,
                      "default_package": False, "production_neutral_art": False, "reference_fidelity": False, "full_engine": False},
            "Ordinary startup evidence overstates delivery")
    require(changed(images["main-menu"], images["main-material-menu"]) == 0, "Optional cold collection changed the normal menu")
    differences = {name: changed(images["main-menu"], images[name]) for name in ("main-world", "main-material-world")}
    require(all(count > 1000 for count in differences.values()), "Actual world versus menu appearance is vacuous")
    differences["ordinary_vs_material_world"] = changed(images["main-world"], images["main-material-world"])
    return {"pixels": pixel_rows, "world_scene_pixels": scene_rows, "menu_world_changed_pixels": differences,
            "hardware": renderer_rows[0], "file_count": 27,
            "output_bytes": total, "profile_count": 4, "process_count": 4,
            "scope": "ordinary automatic main captures by reference; world clock is live; material draws/FPS/package/reference remain unproven"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("main_build_root", "material_root", "first_native_root", "second_native_root"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    report, accepted, required = main_build(args.main_build_root, args.material_root)
    scene_qualification = qualify_scene_crop(Path(report["accepted_build_root"]))
    scene_negatives = scene_negative_controls()
    require(not same(args.first_native_root, args.second_native_root), "Native repeats must have distinct retained roots")
    first = native(args.first_native_root, args.main_build_root, args.material_root, report, accepted, required)
    second = native(args.second_native_root, args.main_build_root, args.material_root, report, accepted, required)
    require(first["hardware"] == second["hardware"], "Hardware/presentation changed between native repeats")
    for name in ("main-menu", "main-material-menu"):
        require(first["pixels"][name]["rgba_sha256"] == second["pixels"][name]["rgba_sha256"], "Normal menu pixels changed between repeats")
    print(json.dumps({"complete": True, "actual_process_count": 8, "actual_capture_count": 8,
                     "scene_qualification": scene_qualification, "scene_negative_controls": scene_negatives,
                     "first": first, "second": second},
                     ensure_ascii=False, sort_keys=True))
    print("Actual main capture evidence checks passed")


if __name__ == "__main__":
    main()
