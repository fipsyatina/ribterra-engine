"""Separate queued-admission/CPU-UV proof for ordinary main and House removal; no full-engine/FPS acceptance."""
from __future__ import annotations

import argparse
import copy
import importlib.util
import sys
sys.dont_write_bytecode = True
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
MAIN_SHA = "e0f4c8fe69deb7691a4945a95654ffbf08ffaa7befd8f3cb186c45004d8415ed"
HELPER_SHA = "d8dd89d638766c5c3ad97bf9d2495108188201ecbd5ac00a55db1bb657b151ce"
CASES = ("main-menu", "main-material-menu", "main-world", "main-material-world", "main-neutral-house")
NEUTRAL_READER_SHA = "8a8622782b960f676e2f324a04b17ef1b2161caf84dd08de7e6bf7bde2e2866f"
NEUTRAL_REVIEW_READER_SHA = "9a134d1a23cdf986e1cb6d20f229b9427ae21adb483325341b191435dfb4a1dd"
ORIGINAL_MAIN_READER_SHA = "23091b6e4cdcf24728606e7060cee2a2514a4b759ce9ed7bd55c3b2250093a2e"
ORIGINAL_MAIN_READER_BYTES = 68836
PREVIOUS_MAIN_REVIEW_READER_SHA = "050c67cc00387021642b3d4422c8b9bb079e137cfbb15019ec2e9aa335f623e6"
PREVIOUS_MAIN_REVIEW_READER_BYTES = 70001
LIMIT = 128 * 1024 * 1024
HEADROOM = 384 * 1024 * 1024
# Conservative central region inside the source-qualified HUD-free rectangle.
SCENE_CROP = (500, 200, 1080, 650)
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
              '/DRIBTERRA_PROBE_BUILD="gameview-neutral-material-isolated-unversioned"']
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
    projection = (accepted_root / "source/include/ribterra/world_projection.hpp").read_text(encoding="utf-8")
    render_frame = (accepted_root / "source/include/ribterra/render_frame.hpp").read_text(encoding="utf-8")
    lights = (accepted_root / "source/include/ribterra/surface_lighting.hpp").read_text(encoding="utf-8")
    building = (accepted_root / "source/include/ribterra/building_catalog.hpp").read_text(encoding="utf-8")
    visuals = (accepted_root / "source/include/ribterra/building_visual.hpp").read_text(encoding="utf-8")
    require("constexpr std::uint32_t logical_height = 900;" in renderer and
            "return {std::max(1440u, fitted_width), logical_height};" in renderer and
            "float scale{1};" in ui and "context->SetDensityIndependentPixelRatio(scale);" in ui,
            "Compiled logical extent/default UI density contract differs")
    require("reference_tile_width = 58.f;" in projection and "reference_tile_height = 29.f;" in projection and
            "legacy_elevation_screen_scale = 2.4f;" in projection and "enum class Pass { Terrain, WorldOrdered, Atmosphere, Fog, Ui };" in render_frame and
            "receiver_mask{1u}" in render_frame and "surface_light_capacity = 4;" in lights and
            '{BuildingKind::House, "house", "hud.house", "House", "Будинок", {30, 15, 0}, 76}' in building and
            "kind == BuildingKind::House ? 2.1f" in visuals,
            "Compiled projection/house/draw-role constants differ from independent geometry math")

    def value(selector: str, property_name: str) -> float:
        blocks = re.findall(r"\.hud-v01 #" + re.escape(selector) + r"\s*\{([^}]+)\}", css)
        pattern = r"(?:^|;)\s*" + re.escape(property_name) + r"\s*:\s*([0-9]+(?:\.[0-9]+)?)(?:dp|px)\s*;"
        # Shared panel-decoration rules precede the controls' actual geometry.
        property_value = next((match for block in blocks if (match := re.search(pattern, block)) is not None), None)
        require(property_value is not None, "HUD source extent missing: " + selector + "/" + property_name)
        return float(property_value[1])

    # Fresh profiles have no settings or opened panels. --demo focuses a resident;
    # the crop also excludes the ordinary bottom-left selection inspector.
    # At1440px the1250/1120px media overrides are inactive and density is1.
    # Top resource/calendar bounds end at88; warning begins at1104. The
    # minimap starts at1124/y696 and bottom actions begin at790. The chosen
    # crop leaves margins on each edge and also ends below any top warning.
    top_end = max(value(s, "top") + value(s, "height") for s in ("topbar", "layerbar"))
    warning_left = 1440 - value("food-warning", "right") - value("food-warning", "width")
    minimap_left = 1440 - value("controls", "right") - value("controls", "width")
    minimap_top = 900 - value("controls", "bottom") - value("controls", "height")
    actions_top = 900 - value("action-strip", "bottom") - value("action-strip", "height")
    inspector = re.search(r"\.hud-v01 \.hud-v01-inspector,\s*\.hud-v01 #controls\s*\{([^}]+)\}", css)
    require(inspector is not None and "box-sizing: border-box;" in inspector[1] and
            "const bool large = scale >= 1.15f;" in ui, "Default inspector extent/density contract differs")
    inspector_right = value("building-summary", "left") + value("building-summary", "width")
    require(SCENE_CROP[1] > top_end and SCENE_CROP[2] < min(warning_left, minimap_left) and
            SCENE_CROP[3] < min(minimap_top, actions_top) and SCENE_CROP[0] > inspector_right,
            "Central scene crop intersects ordinary HUD source bounds")
    return {"crop": list(SCENE_CROP), "logical_size": [1440, 900], "ui_density": 1,
            "top_panels_end": top_end, "warning_left": warning_left, "minimap_left": minimap_left,
            "minimap_top": minimap_top, "actions_top": actions_top, "inspector_right": inspector_right,
            "scope": "fresh automatic profile; demo may select resident; no opened panels; original bound HUD styles"}


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



def checked_readers(accepted: dict):
    # Each imported pure reader and all its dependencies are current bytes in
    # the accepted native source snapshot. No CLI, exporter or app is invoked.
    rows = {row["relative_path"]: row for row in accepted["source_provenance"]}
    for name in ("gameview_neutral_material_evidence_tests.py", "gameview_cumulative_evidence_tests.py",
                 "gameview_ui_evidence_tests.py", "gameview_material_semantics_tests.py",
                 "gameview_neutral_material_export_tests.py", "gameview_material_evidence_tests.py",
                 "gameview_opacity_evidence_tests.py", "gameview_session_time_evidence_tests.py",
                 "gameview_ui_scene_evidence_tests.py"):
        row = rows["tests/" + name]
        require(same(row["repository_path"], REPOSITORY / "tests" / name), "Pure reader origin differs")
        hash_row(row, ("repository_path", "snapshot_path"))
    path = REPOSITORY / "tests/gameview_neutral_material_evidence_tests.py"
    require(digest(path) == NEUTRAL_READER_SHA, "Accepted neutral reader changed")
    review_path = REPOSITORY / "tests/gameview_neutral_material_evidence_review_tests.py"
    require(digest(review_path) == NEUTRAL_REVIEW_READER_SHA, "Separate post-build neutral reader changed")
    spec = importlib.util.spec_from_file_location("neutral_main_bound_reader", review_path)
    require(spec is not None and spec.loader is not None, "Pure reader cannot be loaded")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def accepted_repeats(report: dict, roots: tuple[Path, ...], material: Path, neutral) -> tuple[dict, dict, set[str]]:
    build, objects = Path(report["accepted_build_root"]), Path(report["accepted_object_root"])
    accepted = load(build / "build-report.json")
    kit, ui, painted = (Path(accepted[key]["root"]) for key in ("control_kit", "ui_kit", "material_painted_compatibility"))
    production, checked = neutral.verify_build(build, objects, kit, ui, material, painted)
    require(production == accepted, "Referenced native build differs")
    # Each native has the same build inputs; verified outputs separately join
    # the larger ordinary-main input closure without becoming another run's inputs.
    build_required = frozenset(checked)
    positives, colds, witnesses, sources = [], [], [], None
    for index, output in enumerate(roots):
        profile = "positive" if index < 2 else "cold"
        native = neutral.verify_native(build, objects, kit, ui, material, painted, profile, output, accepted, set(build_required))
        actual_sources = neutral.verify_common(native["deterministic"], build, ui, material, painted, profile)
        if profile == "positive":
            witnesses.append(neutral.verify_positive(output, native["deterministic"], actual_sources)); positives.append(native)
            sources = actual_sources
        else:
            neutral.verify_painted_default(painted, native["deterministic"]["painted_default_compatibility"]); colds.append(native)
        neutral.semantic_negatives(native, actual_sources)
        checked.update(Path(row["path"]) for row in neutral.inventory(output, neutral.cold_aliases(output, material) if profile == "cold" else {}))
    require(witnesses[0] == witnesses[1], "Accepted positive semantic repeats differ")
    for pair, profile in ((positives, "positive"), (colds, "cold")):
        require(neutral.repeat_semantics(pair[0]) == neutral.repeat_semantics(pair[1]), "Accepted neutral semantic repeats differ")
        first, second = roots[:2] if profile == "positive" else roots[2:]
        for row in pair[0]["deterministic"]["output_inventory_excluding_report"]:
            if row["path"] not in neutral.SAVES:
                require((first / row["path"]).read_bytes() == (second / row["path"]).read_bytes(), "Accepted neutral repeat file differs")
    require(sources is not None, "No accepted generated sources")
    return accepted, sources, {os.path.normcase(str(path)) for path in checked}


def main_build(root: Path, material: Path, roots: tuple[Path, ...]) -> tuple[dict, dict, set[str], dict, object]:
    require(same(root.parent, REPOSITORY / "build") and same(material.parent, REPOSITORY / "build"), "Main/material roots escape build")
    report_path = root / "main-build-report.json"; report = load(report_path)
    require(report["complete"] is True and same(report["authoritative_repository"], REPOSITORY) and same(report["output_root"], root) and
            report["compiled_translation_unit_count"] == 1 and report["reused_production_object_count"] == 23 and
            report["shader_file_count"] == 93 and report["shader_compilation"] is False and report["execution"] == {"game": False, "gpu": False},
            "Fresh actual one-main/23-object cohort required")
    build, objects = Path(report["accepted_build_root"]), Path(report["accepted_object_root"])
    require(all(same(path.parent, REPOSITORY / "build") for path in (build, objects)) and not same(build, objects), "Build/object roots conflict")
    require(same(report["accepted_build_report"]["path"], build / "build-report.json") and
            same(report["accepted_artifact_storage"]["path"], build / "artifact-storage.json"), "Accepted artifact linkage differs")
    neutral = checked_readers(load(build / "build-report.json"))
    accepted, sources, required = accepted_repeats(report, roots, material, neutral)
    required.add(os.path.normcase(str(REPOSITORY / "tests/gameview_neutral_material_evidence_review_tests.py")))
    require(same(accepted["object_root"], objects) and same(accepted["material_collection"]["root"], material), "Actual accepted closure differs")
    external = [REPOSITORY / ".deps/SDL3-3.4.16/include", REPOSITORY / ".deps/SDL3_ttf-3.2.2/include",
                REPOSITORY / "vcpkg_installed/x64-windows-release/include/GameNetworkingSockets", REPOSITORY / "vcpkg_installed/x64-windows-release/include"]
    flags = BASE_FLAGS + ["/I" + str(build / "source/include"), "/I" + str(build / "generated")] + ["/external:I" + str(path) for path in external] + ["/external:W0"]
    require(report["flags"] == accepted["flags"] == flags and report["compiler_environment"] == accepted["compiler_environment"] and
            set(report["compiler_environment"]) == ENVIRONMENT and all(report["compiler_environment"].values()), "Main strict environment differs")
    production = [str(objects / f"{name}.obj") for name in PRODUCTION]
    require([row["path"] for row in report["reused_objects"]] == production and report["reused_objects"] == accepted["objects"][:-1] and
            same(report["excluded_object"], objects / "gameview_neutral_material_probe.obj") and
            same(report["compiled_object"]["path"], root / "obj/main.obj"), "Main production object reuse differs")
    tools = {Path(row["path"]).name: row for row in accepted["tools"]}
    toolchain = report["toolchain"]
    for label, tool in (("compiler", "cl.exe"), ("linker", "link.exe")):
        require({key: toolchain[label][key] for key in ("path", "bytes", "sha256")} ==
                {key: tools[tool][key] for key in ("path", "bytes", "sha256")}, "Main tools differ from reused objects")
    require(toolchain["include_environment"] == report["compiler_environment"]["include"] and
            toolchain["lib_environment"] == report["compiler_environment"]["lib"] and
            toolchain["visual_studio"] == report["compiler_environment"]["vs_install_dir"] and
            toolchain["vc_tools_version"] == report["compiler_environment"]["vc_tools_version"] and
            toolchain["windows_sdk_directory"] == report["compiler_environment"]["windows_sdk_dir"] and
            toolchain["windows_sdk_version"] == report["compiler_environment"]["windows_sdk_version"] and
            all(isinstance(toolchain[key], str) and toolchain[key] for key in ("compiler_product_version", "linker_product_version")) and
            toolchain["header_scope"] == "four explicit external include trees content-bound; implicit MSVC/WindowsSDK include path/version identity only",
            "Main explicit/implicit toolchain scope differs")
    require(len(report["commands"]) == 2 and report["link_libraries"] == accepted["link_libraries"], "Main compile/link scope differs")
    command(report["commands"][0], Path(tools["cl.exe"]["path"]), flags + ["/c", str(root / "source/src/client/main.cpp"), "/Fo" + str(root / "obj/main.obj")], root, "compile-main")
    command(report["commands"][1], Path(tools["link.exe"]["path"]), ["/nologo", "/machine:x64", "/incremental:no", "/debug:none", "/subsystem:console",
            "/out:" + str(root / "Ribterra.exe"), str(root / "obj/main.obj"), *production, *[row["path"] for row in accepted["link_libraries"]]], root, "link-Ribterra")
    require(same(report["main_source"]["path"], REPOSITORY / "src/client/main.cpp") and report["main_source"]["sha256"].lower() == MAIN_SHA and
            same(report["executable"]["path"], root / "Ribterra.exe"), "Main source/EXE identity differs")
    snapshots = report["source_provenance"]
    expected = {"main.cpp": ("src/client/main.cpp", MAIN_SHA), "build_gameview_neutral_material_main_isolated.ps1": ("tools/build_gameview_neutral_material_main_isolated.ps1", HELPER_SHA)}
    require(len(snapshots) == 2 and {Path(row["origin"]).name for row in snapshots} == set(expected), "Exact main/helper snapshots required")
    for row in snapshots:
        relative, sha = expected[Path(row["origin"]).name]
        require(same(row["origin"], REPOSITORY / relative) and same(row["destination"], root / "source" / relative) and row["sha256"].lower() == sha, "Reviewed main/helper pin differs")
    immutable = load(Path(report["immutable_input_bindings"]["path"])); final = load(Path(report["immutable_input_final_binding"]["path"]))
    require(len(immutable) == len(final) == len({row["path"].casefold() for row in immutable}) and
            {row["path"] for row in immutable} == {row["path"] for row in final}, "Main build input membership differs")
    required.update(bindings(report) | bindings(immutable) | bindings(final)); required.add(os.path.normcase(str(report_path)))
    required.update(bindings(load(Path(report["preservation_after"]["path"]))))
    tree(build, load(Path(report["accepted_build_tree"]["path"]))); tree(objects, load(Path(report["accepted_object_tree"]["path"])))
    preservation = load(Path(report["preservation_after"]["path"]))
    require(preservation["complete"] is True and preservation["errors"] == [] and preservation["accepted_build_tree_unchanged"] is
            preservation["accepted_object_tree_unchanged"] is True and preservation["current_whole_include_membership_checked"] is
            preservation["external_headers_and_asset_membership_checked"] is preservation["temporary_environment_restored"] is True,
            "Actual main build preservation failed")
    require(report["referenced_assets"] == {label: accepted[key] for label, key in
            (("control", "control_kit"), ("ui", "ui_kit"), ("original", "original_art"), ("neutral", "material_collection"), ("painted", "material_painted_compatibility"))} and
            report["producer_source_inputs"] == {"neutral": accepted["material_source_inputs"], "painted": accepted["painted_source_inputs"]} and
            report["accepted_protected_bindings"] == accepted["protected_binding"], "Main complete catalog/source/launcher bindings differ")
    require(len(report["shader_files"]) == 93 and set(files(root / "assets/shaders")) == set(files(build / "assets/shaders")) and all(
            same(row["destination"], root / "assets/shaders" / Path(row["origin"]).name) and same(Path(row["origin"]).parent, build / "assets/shaders")
            for row in report["shader_files"]), "Main93 shader copies differ")
    require(len(report["runtime_files"]) == 3 and all(same(row["destination"], root / name) and same(row["origin"], build / name)
            for row, name in zip(report["runtime_files"], ("SDL3.dll", "SDL3_ttf.dll", "assets/fonts/NotoSans-Regular.ttf"), strict=True)), "Main runtime copies differ")
    tree(root, [*report["output_files_before_report"], {"path": str(report_path), "relative_path": report_path.name, "bytes": report_path.stat().st_size, "sha256": digest(report_path)}])
    counts = {"main": sum(path.stat().st_size for path in files(root).values()), "build": neutral.total(neutral.inventory(build)), "objects": neutral.total(neutral.inventory(objects))}
    require(all(size <= LIMIT for size in counts.values()) and report["storage"]["output_bytes"] == counts["main"] and
            report["storage"]["output_file_count"] == len(files(root)) and report["storage"]["aggregate_bytes"] == sum(counts.values()) and
            report["storage"]["aggregate_limit_claimed"] is False and report["storage"]["maximum_bytes_each"] == LIMIT,
            "Main individual/aggregate storage accounting differs")
    storage = report["storage"]
    require(storage["maximum_output_bytes"] == LIMIT and storage["minimum_free_bytes"] == HEADROOM and
            storage["accepted_build_bytes"] == counts["build"] and storage["accepted_object_bytes"] == counts["objects"] and
            storage["bulk_art_copied"] is storage["sdk_libraries_copied"] is storage["objects_copied"] is False and
            storage["failed_roots_retained"] is storage["report_included"] is True and
            type(storage["observed_free_bytes"]) is int and storage["observed_free_bytes"] >= HEADROOM,
            "Main compile runtime-storage scope/headroom differs")
    require(report["dependency_closure"] == {"accepted_source_count": len(accepted["source_provenance"]),
            "whole_current_include_count": len(files(REPOSITORY / "include")), "external_sdk_tree_count": 4,
            "accepted_actual_command_count": 122, "accepted_fresh_object_count": 24, "main_compiled_fresh": True,
            "source_snapshot_current_exact": True, "probe_only_inline_on_main": False, "implicit_sdk_identity_only": True},
            "Main complete production/header/SDK reuse scope differs")
    return report, accepted, required, sources, neutral

def valid_frame(frame: dict, require_typed: bool = True) -> None:
    require(set(frame) == {"content", "size", "clear", "hdr", "exposure", "indices", "vertices", "surfaces", "commands", "sprites", "lighting", "lights",
            "water_normal", "ground_materials", "contacts", "occluders", "sky_triangles", "reflections", "water_surface", "atmosphere", "moon", "contact_projection"},
            "Full ordinary Frame serialization omitted or added payload")
    require(frame["content"] == 0 and frame["size"] == [1440, 900] and frame["hdr"] is True and
            type(frame["exposure"]) in (int, float) and math.isfinite(frame["exposure"]) and frame["exposure"] > 0 and
            frame["water_normal"] == [0, 0] and all(frame[key] == [] for key in ("ground_materials", "contacts", "occluders", "sky_triangles", "reflections")) and
            all(frame[key] is None for key in ("water_surface", "atmosphere", "moon", "contact_projection")), "House frame is not ordinary LegacyCombined")
    vertices, indices, surfaces = frame["vertices"], frame["indices"], frame["surfaces"]
    require(len(vertices) == len(surfaces) > 0 and all(surface == [[0., 0., 0.], [0., 0., 1.]] for surface in surfaces), "Ordinary companions differ")
    for vertex in vertices:
        require(len(vertex) == 8 and all(type(value) in (int, float) and math.isfinite(value) for value in vertex[:4]) and
                all(type(value) is int and 0 <= value <= 255 for value in vertex[4:]), "Invalid ordinary vertex")
    require(all(type(index) is int and 0 <= index < len(vertices) for index in indices), "Invalid ordinary index")
    ui = plain = typed = 0
    for entry in frame["commands"]:
        require(set(entry) in ({"geometry"}, {"text"}), "Unknown ordinary command")
        if "geometry" in entry:
            draw = entry["geometry"]
            require(len(draw) == 14 and type(draw[4]) is type(draw[5]) is int and 0 <= draw[4] <= len(indices) and
                    0 <= draw[5] <= len(indices)-draw[4] and draw[5] % 3 == 0 and type(draw[12]) is int and
                    0 <= draw[12] <= len(frame["sprites"]), "Ordinary draw range/material index invalid")
            require(type(draw[0]) is type(draw[1]) is int and 0 <= draw[0] <= 4 and 0 <= draw[1] <= 3 and
                    all(type(value) is int and value >= 0 for value in draw[2:4]) and
                    all(type(value) in (int, float) and math.isfinite(value) for value in draw[6:10]) and
                    type(draw[10]) is type(draw[11]) is bool and draw[13] == 0,
                    "Ordinary draw enum/texture/scissor/ground carrier invalid")
            if draw[0] == 4:
                require(draw[12] == 0 and draw[11] is False, "UI command acquired a World material")
                ui += int(draw[5] > 0)
            elif draw[12]: typed += 1
            else: plain += int(draw[5] > 0)
        else:
            require(len(entry["text"]) == 10, "Ordinary text serialization differs")
    require(ui > 0 and plain > 0 and (typed > 0 or not require_typed), "House frame omitted real World/UI/typed roles")
    require(len(frame["lighting"]) == 4 and len(frame["lights"]) == 4 and
            all(len(row) == 4 for row in frame["lighting"]) and
            all(len(light) == 5 and all(len(row) == 4 for row in light) for light in frame["lights"]), "Full light carrier serialization differs")
    for row in frame["lighting"][:3] + [row for light in frame["lights"] for row in light]:
        require(all(type(value) in (int, float) and math.isfinite(value) for value in row), "Nonfinite full light carrier")
    require(all(type(value) is int and 0 <= value <= (1 << 32)-1 for value in frame["lighting"][3]) and
            frame["lighting"][3][0] <= 4, "Invalid full light count carrier")
    for material in frame["sprites"]:
        require(len(material) == 12 and all(type(value) is int and value > 0 for value in material[:2]) and
                len(material[2]) == 5 and all(len(row) == 4 for row in material[2]) and
                all(len(material[index]) == 4 for index in (3, 4, 5, 6, 7)) and
                all(type(value) in (int, float) and math.isfinite(value) for row in [*material[2], *material[3:8]] for value in row),
                "Invalid full SpriteMaterial carrier")


def house_geometry(report: dict, source: dict, neutral) -> tuple[list[float], dict]:
    require(report["schema"] == 1 and report["mode"] == "diagnostic-house-capture" and report["complete"] is True and
            report["stage"] == "actual_paired_capture_complete" and report["seed"] == 70421 and report["advance_limit"] == 12000 and
            report["world_fixture_imported"] is report["weather_overridden"] is report["resources_created"] is False and
            report["source_preimage_sha256"] == "8468889E5062D8207434604D2B59D25270CAA84C91231A3BB93FA92B868D063D" and
            report["fps"] is report["gpu_execution_ms"] is None and report["reference_fidelity"] == report["full_engine"] == "OPEN",
            "Real construction/source/scope contract differs")
    house, weather, admission = report["house"], report["weather"], report["admission"]
    require(report["initial_tick"] == 0 and report["camp"] == [1, 24, 24] and report["initial_buildings"] == [[1, 0, 24, 24, 1.]] and
            report["initial_resources"] == [80, 40, 100] and type(report["advance_attempts"]) is int and
            1 <= report["advance_attempts"] <= 12000 and report["latest_tick"] == report["advance_attempts"] and report["applied_reply"] is True,
            "House was not earned from the ordinary Camp-only world")
    require(house["kind"] == 1 and house["layer"] == 0 and house["id"] == 2 and house["progress"] == 1. and house["level"] == 1 and
            house["nation"] == 1 and house["resolved_key"] == "building.house.ready" and admission["site"] == house["cell"] and
            admission["accepted"] is True and admission["failure"] == 0 and type(admission["path_batch_id"]) is int and
            admission["path_batch_id"] == 0 and admission["pending"] is True,
            "Completed House identity/real admission differs")
    require(report["site_search"]["bounds"] == [21, 22, 27, 27] and type(report["site_search"]["examined"]) is int and
            1 <= report["site_search"]["examined"] <= 42 and all(type(v) is int for v in house["cell"]) and
            21 <= house["cell"][0] <= 27 and 22 <= house["cell"][1] <= 27, "Real bounded legal-site search differs")
    replies = report["replies"]
    require(replies and any(row["status"] == 2 for row in replies) and all(row["request_id"] == 1 and row["author"] == 1 and
            row["status"] in (1, 2) and row["failure"] == 0 and 0 <= row["applied_tick"] <= report["latest_tick"] for row in replies), "Actual authority Applied reply not proved")
    # Ordinary simulation may earn resources during construction. The pinned
    # main starts with exact stock and admits only the real build/advance APIs;
    # a completion-stock increase is not evidence of an injected inventory.
    require(len(report["completion_resources"]) == 3 and all(type(value) is int and 0 <= value <= (1 << 31)-1
            for value in report["completion_resources"]), "Invalid actual completion inventory counters")
    require(weather["known"] is True and weather["simulation_tick"] == weather["evaluated_simulation_tick"] == report["latest_tick"] and
            weather["calendar_tick"] == weather["simulation_tick"] + weather["calendar_offset_ticks"] and
            weather["algorithm_version"] == weather["climate_profile"] == 1 and weather["scenario"] == weather["calendar_offset_ticks"] == 0 and
            type(weather["kind"]) is int and 0 <= weather["kind"] <= 3 and type(weather["precipitation"]) is int and 0 <= weather["precipitation"] <= 2 and
            type(weather["wind_direction"]) is int and 0 <= weather["wind_direction"] <= 7 and
            all(type(weather[key]) is int and 0 <= weather[key] <= 1000 for key in
                ("precipitation_permille", "wind_permille", "wetness_permille", "snow_cover_permille")) and
            type(weather["wetness_remainder"]) is int and -28800 < weather["wetness_remainder"] < 28800 and
            type(weather["temperature_deci_c"]) is int and -32768 <= weather["temperature_deci_c"] <= 32767 and
            type(weather["outdoor_work_rate_bp"]) is int and 0 <= weather["outdoor_work_rate_bp"] <= 10000,
            "Diagnostic used unknown/stale/nonstandard weather authority")
    cam, foundation = report["camera"], report["foundation"]
    require(cam["requested"] == {"SDL_key": "C", "center": [24, 24], "zoom": 1.2999999523162842} and
            cam["paused_via_SDL_SPACE"] is True and cam["selected_buildings"] == [] and report["logical_size"] == [1440, 900], "Actual SDL camera/pause contract differs")
    f32 = neutral.f32; close = neutral.close_values
    close(cam["effective"]["center"], [24., 24.], .001); close([cam["effective"]["zoom"]], [f32(1.3)], .00001)
    corners = foundation["corners"]
    require(len(corners) == 4 and all(type(v) in (int, float) and math.isfinite(v) and 0 <= v <= 32767 for v in corners), "Reported actual foundation corners invalid")
    # At integer cell centre u=v=.5, on the source NW-SE diagonal. Current
    # authoritative corners are captured by the compiled main, not replayed here.
    height = f32(f32(corners[0] * .5) + f32(corners[2] * .5)); base = max(corners); z = f32(1.3)
    close([foundation["ground_height"], foundation["base"]], [height, base], .00001)
    x, y = house["cell"]
    tile = [f32(f32(1440. * f32(.48)) + f32(f32(f32(x-y) * f32(58.*z)) * f32(.5))),
            f32(f32(f32(900. * f32(.48)) + f32(f32(f32(x+y-48) * f32(29.*z)) * f32(.5))) - f32(f32(height*f32(2.4))*z))]
    close(foundation["tile_screen"], tile, .005)
    ground = [tile[0], f32(f32(tile[1] - f32(f32(f32(base-height)*f32(2.4))*z)) + f32(15.*z))]
    close(foundation["sprite_ground"], ground, .005)
    sw, sh = source["entry"]["rect"][2:]; pivot = [f32(v) for v in source["entry"]["pivot"]]; ref = f32(source["entry"].get("reference_width", sw))
    unzoomed = f32(f32(76.) * f32(2.1)); width = f32(f32(unzoomed*z)*f32(sw/ref)); pixels_h = f32(width*f32(sh/sw))
    bounds = [f32(ground[0]-f32(width*pivot[0])), f32(ground[1]-f32(pixels_h*pivot[1])), width, pixels_h]
    close(foundation["bounds"], bounds, .005)
    require(bounds[0] >= 0 and bounds[1] >= 0 and bounds[0]+bounds[2] <= 1440 and bounds[1]+bounds[3] <= 900, "House body is clipped")
    return bounds, {"house": house, "admission": admission, "replies": replies, "advance_attempts": report["advance_attempts"],
        "latest_tick": report["latest_tick"], "weather": weather, "foundation": foundation}


def command_only_removal(full: dict, removed: dict, index: int) -> None:
    require(type(index) is int and 0 <= index < len(full["commands"]), "Removed body command index invalid")
    expected = copy.deepcopy(full); del expected["commands"][index]
    require(removed == expected, "Pair changed non-House vertices/carriers/commands")


def house_pair(root: Path, report: dict, source: dict, material: Path, neutral, images: dict, build: Path) -> dict:
    # These bytes belong to the independently verified compiled source closure.
    # Qualify queue admission and descriptor resolution without claiming uniform readback.
    paths = {name: build / "source" / relative for name, relative in (
        ("client", "src/client/client_session.cpp"), ("art", "src/client/game_art.cpp"),
        ("renderer", "src/render/renderer2d.cpp"), ("frame", "include/ribterra/render_frame.hpp"))}
    compiled = {name: re.sub(r"\s+", "", path.read_text(encoding="utf-8")) for name, path in paths.items()}
    require("CommandResultClientSession::build(BuildingKindkind,intx,inty,WorldLayerlayer){"
            "returnimpl_->queue(CommandType::Build,{},x,y,kind,layer);}" in compiled["client"] and
            re.search(r'if\(reply\.status==CommandReplyStatus::Queued\)return\{true,"[^"]*",0,true\};', compiled["client"]) is not None,
            "Compiled ClientSession queued-build admission differs")
    require("std::array<std::array<float,4>,5>uv_regions{};" in compiled["frame"] and
            "render::SpriteMaterialDrawmaterial;material.material=sprite.material;" in compiled["art"] and
            "material.uv[c]={float(rect[0])/texture.desc.size.width,float(rect[1])/texture.desc.size.height,"
            "float(rect[2])/texture.desc.size.width,float(rect[3])/texture.desc.size.height};" in compiled["art"] and
            "desc.channels[c]={material_textures[source.textures[c]],source.uv[c]};" in compiled["art"],
            "Compiled CPU material initialization/channel normalization differs")
    require("validated.channels[index].uv={std::round(region.x*width)/width,std::round(region.y*height)/height,"
            "std::round(region.w*width)/width,std::round(region.h*height)/height};" in compiled["renderer"] and
            "SpriteSurfaceGpusurface=instance.surface;for(std::size_tindex=0;index<surface.uv_regions.size();++index){"
            "constauto&uv=desc.channels[index].uv;surface.uv_regions[index]={uv.x,uv.y,uv.w,uv.h};}"
            "surface.emission=desc.emission;surface.emission[3]*=instance.emission_scale;"
            "SDL_PushGPUFragmentUniformData(command,0,&frame.surface_lighting,sizeof(frame.surface_lighting));"
            "SDL_PushGPUFragmentUniformData(command,1,&surface,sizeof(surface));" in compiled["renderer"],
            "Compiled registered UV canonicalization/LegacyCombined upload mapping differs")
    bounds, construction = house_geometry(report, source, neutral)
    item = source["item"]; art = load(material / "art/world.json"); metadata = load(material / "art/material-collection.json")
    sprite = next(row for row in art["lit_sprites"] if row["id"] == item["sprite_id"]); material_row = next(row for row in art["materials"] if row["id"] == item["material_id"])
    original = load(REPOSITORY / "assets/art/world.json"); captured = report["source"]
    expected_source = {"original_content_hash": neutral.manifest_content(load(REPOSITORY / "assets/manifest.json")),
        "collection_content_hash": neutral.manifest_content(load(material / "manifest.json")), "original_world_sha256": digest(REPOSITORY / "assets/art/world.json"),
        "metadata_sha256": digest(material / "art/material-collection.json"), "world_sha256": digest(material / "art/world.json"), "item": item,
        "source_record": neutral.source_record(item), "original_page": source["page"]["path"], "original_entry": source["entry"], "sprite": sprite,
        "material": material_row, "original_crop_rgba_sha256": hashlib.sha256(source["paint"]).hexdigest(),
        "generated_rgba_sha256": hashlib.sha256(source["rgba"]).hexdigest(), "channels": []}
    manifest = {row["id"]: row for row in load(material / "manifest.json")["assets"]}; textures = {row["id"]: row for row in art["textures"]}
    for name in ("albedo", "normal_material", "camera_depth", "emission_mask", "surface_mask", "caster_mask"):
        channel = material_row["channels"][name]; texture = textures[channel["texture"]]; asset = manifest[texture["id"]]
        expected_source["channels"].append({"name": name, "channel": channel, "texture": texture,
            "asset": {key: asset[key] for key in ("id", "path", "bytes", "sha256")}})
    require(captured == expected_source and neutral.source_hash(captured["source_record"]) == item["source_hash"], "Diagnostic full six-channel original/generated source identity differs")
    body = report["body"]; full, removed = body["full_frame"], body["removed_frame"]
    valid_frame(full); valid_frame(removed, require_typed=False)
    index = body["command_index"]
    require(type(index) is int and 0 <= index < len(full["commands"]) and full["commands"][index] == {"geometry": body["command"]}, "Removed command is not the identified House body")
    command_only_removal(full, removed, index)
    require(body["only_house_command_removed"] is body["all_other_payload_exact"] is True and
            hashlib.sha256(neutral.canonical(full)).hexdigest() == body["full_frame_sha256"] and
            hashlib.sha256(neutral.canonical(removed)).hexdigest() == body["removed_frame_sha256"], "Pair changed non-House payload or serialized fingerprint")
    draw = body["command"]
    require(len(draw) == 14 and draw[:4] == [1, 1, 0, 0] and draw[5] == 6 and draw[10:12] == [False, False] and draw[13] == 0 and
            draw[6:10] == [0., 0., 0., 0.] and type(draw[12]) is int and draw[12] == body["material_index"] > 0, "House body crossed draw roles")
    indices = full["indices"][draw[4]:draw[4]+6]; a = indices[0]
    require(indices == body["indices"] == [a,a+1,a+2,a,a+2,a+3] and body["vertex_indices"] == [a,a+1,a+2,a+3] and
            body["vertices"] == [full["vertices"][v] for v in body["vertex_indices"]], "Actual House indices/vertices differ")
    x,y,w,h = bounds; positions = [[x,y], [x+w,y], [x+w,y+h], [x,y+h]]
    for vertex, position, uv in zip(body["vertices"], positions, ([0.,0.],[1.,0.],[1.,1.],[0.,1.]), strict=True):
        neutral.close_values(vertex[:2], position, .005);require(vertex[2:] == [*uv,255,255,255,255], "Actual House UV/white opacity differs")
    actual = body["actual_material"]; f32 = neutral.f32; sw,sh = source["entry"]["rect"][2:]
    require(full["sprites"][draw[12]-1] == [*actual["handle"], actual["uv_regions"], actual["anchor_export"], actual["source_size_pivot"],
            actual["depth_scale"], actual["emission"], actual["response"], actual["reveal"], actual["receiver_mask"], actual["receiver_owner"], actual["emission_scale"]], "House material carrier differs")
    registered_channels = []
    for name in ("albedo", "normal_material", "camera_depth", "emission_mask", "surface_mask"):
        channel = material_row["channels"][name]; texture = textures[channel["texture"]]
        rect, size = channel["rect"], [texture["width"],texture["height"]]; spans = [size[0],size[1],size[0],size[1]]
        normalized = [f32(f32(value)/f32(span)) for value,span in zip(rect,spans,strict=True)]
        registered = [f32(f32(math.floor(f32(value*f32(span))+.5))/f32(span))
                      for value,span in zip(normalized,spans,strict=True)]
        require(normalized == registered == [0.,0.,1.,1.], "Source-derived registered full-channel UV differs")
        registered_channels.append({"channel":name,"texture":channel["texture"],"rect":rect,"size":size,
                                    "normalized_uv":normalized,"registered_uv":registered})
    require(actual["handle"][0] > 0 and actual["handle"][1] > 0 and actual["source_size_pivot"] == [*sprite["source_size"],*sprite["pivot_px"]] and
            actual["uv_regions"] == [[0.,0.,0.,0.]]*5 and actual["emission"] == material_row["emission"] and actual["anchor_export"][3] == sprite["export_pixels_per_reference_pixel"] and
            actual["reveal"] == 1. and actual["receiver_mask"] == 1 and actual["receiver_owner"] == 0 and actual["emission_scale"] == 1., "Delivered House material tuple differs")
    weather = report["weather"]
    wet = f32(min(1., max(0., (weather["wetness_permille"] + weather["wetness_remainder"] / 28800.) / 1000.)))
    require(actual["response"] == [wet, f32(.08), 0., 0.], "House wetness/roughness response differs from current authority")
    expected_scale=f32(f32(f32(76.)*f32(2.1))*sprite["export_pixels_per_reference_pixel"]/f32(source["entry"].get("reference_width",sw)))
    neutral.close_values(actual["depth_scale"][:3], [*sprite["depth_range"],expected_scale], .00001)
    neutral.close_values(actual["anchor_export"][:2], [report["house"]["cell"][0]-24.,report["house"]["cell"][1]-24.], .00001)
    # Render XYZ projection basis: y=(x+y)*14.5 -29*z.
    plane_y=f32(f32(sum(actual["anchor_export"][:2]))*f32(14.5))
    expected_z=f32(f32(plane_y-f32(f32(report["foundation"]["sprite_ground"][1]-f32(900.*f32(.48)))/f32(1.3)))/29.)
    neutral.close_values([actual["anchor_export"][2]], [expected_z], .00001)
    for name, frame in (("full_telemetry",full),("removed_telemetry",removed)):
        t=report[name]; typed=sum("geometry" in cmd and cmd["geometry"][12]>0 for cmd in frame["commands"])
        require(t["submitted"] is t["capture"] is True and t["vertices"] == len(frame["vertices"]) and t["indices"] == len(frame["indices"]) and
                t["material_draws"] == typed and t["material_carrier_rows"] == len(frame["sprites"]) and t["graph_pass_count"] == 0 and
                t["graph_passes"] == [] and all(t[key] == 0 for key in ("ground_draws","contacts","water_draws","reflection_draws")) and t["gpu_execution_ms"] is None,
                "Actual House pair active draw telemetry differs")
    require(report["full_telemetry"]["material_draws"] == report["removed_telemetry"]["material_draws"]+1, "Body removal did not remove one active typed draw")
    names=("main-neutral-house.png","main-neutral-house.house-removed.png")
    require(report["captures"] == [{"path":name,"bytes":(root/name).stat().st_size,"sha256":digest(root/name)} for name in names], "Actual paired PNG binding differs")
    first, second = (images[name.removesuffix('.png')] for name in names)
    raw, raw_removed = first.tobytes(),second.tobytes(); qualified=contribution=total=outside=0
    for py in range(900):
        for px in range(1440):
            offset=(py*1440+px)*4; differs=raw[offset:offset+4]!=raw_removed[offset:offset+4];total+=differs
            xx,yy=px+.5,py+.5
            if not(x <= xx < x+w and y <= yy < y+h):outside+=differs;continue
            sx,sy=math.floor((xx-x)/w*sw),math.floor((yy-y)/h*sh)
            if not(1<=sx<sw-1 and 1<=sy<sh-1):continue
            if all(source['rgba'][((sy+dy)*sw+sx+dx)*4+3]==255 for dy in (-1,0,1) for dx in (-1,0,1)):
                qualified+=1;contribution+=differs
    require(report["pixels"] == {"qualification":"pixel-center source UV; 3x3 generated alpha255; changed only by actual same-frame body removal",
            "qualified":qualified,"changed":contribution,"total_changed":total} and qualified>0 and contribution>0 and outside==0,
            "Specific generated House body pixel contribution is vacuous or escapes its quad")
    return {"construction":construction,"source_record_sha256":neutral.source_hash(captured["source_record"]),
        "generated_rgba_sha256":captured["generated_rgba_sha256"],"bounds":bounds,"actual_material":actual,
        "uv_contract":{"captured_cpu_uv":actual["uv_regions"],"registered_channels":registered_channels,
            "compiled_sources":[{"path":str(path),"bytes":path.stat().st_size,"sha256":digest(path)} for path in paths.values()],
            "scope":"compiled-source inference of registered UV resolution; no GPU-uniform readback"},
        "qualified_pixels":qualified,"changed_pixels":contribution,"total_changed_pixels":total,"outside_quad_changed_pixels":outside,
        "scope":"actual main/session construction; foundation math independently derived from captured current host corners; no historical simulation replay"}


def native(root: Path, main_root: Path, material: Path, prior_roots: tuple[Path, ...], report: dict,
           accepted: dict, required: set[str], sources: dict, neutral) -> dict:
    ledger_path = root / "main-native-execution.json"; ledger = load(ledger_path)
    require(ledger["schema"] == 1 and ledger["complete"] is ledger["accepted"] is ledger["bound_inputs_unchanged"] is True and
            ledger["integrity_errors"] == [] and all(same(ledger[key], value) for key, value in
            (("repository", REPOSITORY), ("main_build_root", main_root), ("accepted_build_root", report["accepted_build_root"]),
             ("accepted_object_root", report["accepted_object_root"]), ("material_root", material), ("output_root", root))),
            "Actual five-case main ledger failed or root identity differs")
    require(ledger["process_count"] == 5 and [row["id"] for row in ledger["commands"]] == list(CASES) and
            ledger["automatic_contract"] == {"window": [1440, 900], "capture_frame_index": 2, "exit_after_frames": 3,
            "seed": 70421, "demo_preadvances": 1700, "house_advance_limit": 12000,
            "profiles": "capture filename + .test-profile- + secure_token(16), retained empty climate-v1",
            "world_frame_repeat_claimed": False, "house_png_repeat_claimed": False}, "Five ordinary process/source contracts differ")
    bindings(ledger)
    for key, name in (("input_bindings", "input-bindings.json"), ("input_trees", "input-trees-before.json"),
                      ("input_final_binding", "input-final-hashes.json"), ("preservation", "preservation-after.json")):
        require(same(ledger[key]["path"], root / name), "Native evidence filename differs")
    require(same(ledger["main_build_report"]["path"], main_root / "main-build-report.json"), "Native main build linkage differs")
    before, after = load(Path(ledger["input_bindings"]["path"])), load(Path(ledger["input_final_binding"]["path"]))
    require(len(before) == len(after) == len({row["path"].casefold() for row in before}) and
            {row["path"] for row in before} == {row["path"] for row in after}, "Native full input membership differs")
    require(required <= bindings(before) | bindings(after), "Main execution omitted a complete strict/native dependency binding")
    prior = {row["path"]: row for row in before}
    for row in after:
        require(set(row) == {"path", "bytes", "sha256", "after_bytes", "after_sha256", "unchanged"} and
                row["unchanged"] is True and row["bytes"] == row["after_bytes"] == prior[row["path"]]["bytes"] and
                row["sha256"] == row["after_sha256"] == prior[row["path"]]["sha256"], "Actual after-input observation differs")
    protected = load(Path(ledger["input_trees"]["path"]))
    expected_roots = {str(main_root), report["accepted_build_root"], report["accepted_object_root"], *map(str, prior_roots),
        str(REPOSITORY / "include"),
        *[accepted[name]["root"] for name in ("control_kit", "ui_kit", "original_art", "material_collection", "material_painted_compatibility")],
        *[row["root"] for row in accepted["external_header_trees"]["trees_before"]]}
    require(len(protected) == len({row["root"].casefold() for row in protected}) and
            {row["root"].casefold() for row in protected} == {value.casefold() for value in expected_roots}, "Native protected membership closure differs")
    for row in protected:
        path = Path(row["root"])
        aliases = neutral.cold_aliases(path, material) if path in prior_roots[2:] else {}
        require(row["aliases"] == {key.relative_to(path).as_posix(): str(value) for key, value in aliases.items()}, "Protected aliases differ")
        if aliases:
            actual_rows = neutral.inventory(path, aliases)
            require(neutral.indexed_inventory(row["files"]) == neutral.indexed_inventory(actual_rows), "Cold protected input tree differs")
        else:
            tree(path, row["files"])
    repeats = ledger["accepted_neutral_repeats"]
    require(len(repeats) == 4, "Four accepted native repeats missing")
    for index, row in enumerate(repeats):
        profile = "positive" if index < 2 else "cold"; path = prior_roots[index]
        require(row["profile"] == profile and same(row["root"], path) and same(row["execution"]["path"], path / "native-execution.json") and
                same(row["report"]["path"], path / "neutral-material-report.json"), "Accepted repeated-root linkage differs")
    preservation = load(Path(ledger["preservation"]["path"]))
    require(preservation["complete"] is True and preservation["errors"] == [] and preservation["protected_tree_count"] == len(protected), "Native preservation failed")
    prefs = preservation["ordinary_preferences"]
    require(same(prefs["root"], Path(os.environ["APPDATA"]) / "Ribterra/Ribterra") and
            prefs["existed_before"] is prefs["existed_after"] is True and
            prefs["files_before"] == prefs["files_after"], "Ordinary preferences changed")
    for observation in ("files_before", "files_after"):
        rows = prefs[observation]
        require(isinstance(rows, list) and len(rows) == 36 and
                len({row["relative_path"] for row in rows}) == len({row["relative_path"].casefold() for row in rows}) == 36 and
                len({row["path"] for row in rows}) == len({row["path"].casefold() for row in rows}) == 36,
                "Exactly36 unique/casefold-unique ordinary preference files required")
        tree(Path(prefs["root"]), rows)
    names = {"run_gameview_neutral_material_main_native.ps1", "gameview_neutral_material_main_evidence_tests.py"}
    require(len(ledger["source_provenance"]) == 2 and {Path(row["origin"]).name for row in ledger["source_provenance"]} == names, "Native reader/runner snapshots missing")
    for row in ledger["source_provenance"]:
        name = Path(row["origin"]).name
        require(same(row["origin"], REPOSITORY / ("tools" if name.endswith(".ps1") else "tests") / name) and
                same(row["destination"], root / "source" / name), "Native runner/reader snapshot origin differs")
        if name == "gameview_neutral_material_main_evidence_tests.py":
            require(row["bytes"] == ORIGINAL_MAIN_READER_BYTES and row["sha256"].lower() == ORIGINAL_MAIN_READER_SHA,
                    "Original compiled main reader snapshot identity differs")
    images, pixel_rows, scenes, profiles, hardware_rows = {}, {}, {}, [], []
    expected_files = {"main-native-execution.json", "input-bindings.json", "input-final-hashes.json", "input-trees-before.json", "preservation-after.json",
                      *["source/" + name for name in names], "main-neutral-house.house-removed.png", "main-neutral-house.house-diagnostic.json"}
    for case in ledger["commands"]:
        name = case["id"]; capture = root / (name + ".png"); info_path = root / (name + ".renderer-info.json")
        argv = ["--catalog-assets", str(REPOSITORY / "assets"), "--renderer-info", str(info_path)]
        if name == "main-neutral-house":
            argv += ["--diagnostic-house-capture", "--capture", str(capture), "--material-collection", str(material)]
        else:
            argv += ["--menu-capture", str(capture)] if name.endswith("menu") else ["--demo", "--capture", str(capture)]
            if name.startswith("main-material-"):
                argv += ["--material-collection", str(material)]
        require(case["arguments"] == argv and case["actual_argv"] == [str(main_root / "Ribterra.exe"), *argv] and
                same(case["executable"]["path"], main_root / "Ribterra.exe") and same(case["working_directory"], main_root) and
                case["actual_subprocess"] is True and type(case["process_id"]) is int and case["process_id"] > 0 and
                type(case["exit_code"]) is int and case["exit_code"] == 0 and case["timed_out"] is False and case["launch_error"] == "" and
                type(case["elapsed_milliseconds"]) in (int, float) and math.isfinite(case["elapsed_milliseconds"]) and case["elapsed_milliseconds"] >= 0,
                "Actual ordinary subprocess/exit/argv differs")
        require(load(root / ("case-" + name + ".json")) == {key: value for key, value in case.items() if key not in
                {"capture", "renderer_info", "profile", "removed_capture", "house_report"}}, "Retained process case ledger differs")
        for key, filename in (("log", name + ".log"), ("exit_sidecar", name + ".log.exit.txt"), ("capture", name + ".png"),
                              ("renderer_info", name + ".renderer-info.json")):
            require(same(case[key]["path"], root / filename), "Process output filename differs")
        require(Path(case["exit_sidecar"]["path"]).read_text(encoding="utf-8-sig").strip() == "0", "Actual exit sidecar differs")
        profile = case["profile"]; profile_path = Path(profile["root"])
        require(same(profile_path.parent, root) and profile_path.name == profile["relative_path"] and
                re.fullmatch(re.escape(name) + r"\.png\.test-profile-[0-9a-f]{32}", profile_path.name) is not None and
                profile["files"] == [] and profile["directories"] == ["climate-v1"] and files(profile_path) == {} and
                {path.name for path in profile_path.iterdir()} == {"climate-v1"} and (profile_path / "climate-v1").is_dir(), "Automatic isolated profile differs")
        no_reparse(profile_path / "climate-v1"); require(list((profile_path / "climate-v1").iterdir()) == [], "Automatic profile contains unexpected directories")
        profiles.append(profile_path.name); images[name], pixel_rows[name] = pixels(capture)
        if name.endswith("world") or name == "main-neutral-house":
            scenes[name] = scene_pixels(images[name])
        hardware = load(info_path)
        require(hardware["backend"] == "direct3d12" and hardware["software"] is False and hardware["drawable"] == [1440, 900] and
                isinstance(hardware["device"], str) and hardware["device"] and isinstance(hardware["driver"], str) and hardware["driver"] and
                type(hardware["refresh_hz"]) in (int, float) and math.isfinite(hardware["refresh_hz"]) and hardware["refresh_hz"] >= 0,
                "Actual main hardware/drawable differs")
        version = re.search(r'engine_version\s*=\s*"([^"]+)"', (Path(report["accepted_build_root"]) / "source/include/ribterra/foundation.hpp").read_text(encoding="utf-8"))
        require(version is not None and hardware["engine_version"] == version[1] and hardware["window_title"] == "Ribterra " + version[1], "Actual compiled version/title differs")
        presentation = hardware["presentation"]
        require(presentation["requested"] == {"mode": "vsync", "frame_limit": 0, "frames_in_flight": 2} and
                presentation["effective"] == presentation["requested"] and presentation["GPU_controls_available"] is True and
                type(presentation["immediate_supported"]) is type(presentation["mailbox_supported"]) is bool and
                presentation["reason"] == 0 and presentation["error"] == "", "Actual main presentation differs")
        hardware_rows.append({key: value for key, value in hardware.items() if key != "diagnostic_house"})
        expected_files.update({name + suffix for suffix in (".png", ".renderer-info.json", ".log", ".log.exit.txt")}); expected_files.add("case-" + name + ".json")
    require(all(row == hardware_rows[0] for row in hardware_rows), "Hardware/presentation changed between cases")
    require({path.name for path in root.iterdir() if path.is_dir()} == {"source", *profiles} and
            {path.name for path in (root / "source").iterdir()} == names and set(files(root)) == expected_files and len(expected_files) == 34,
            "Exact five profiles/34 native files differ")
    house_report_path = root / "main-neutral-house.house-diagnostic.json"; house_report = load(house_report_path)
    require(same(ledger["commands"][-1]["house_report"]["path"], house_report_path) and
            same(ledger["commands"][-1]["removed_capture"]["path"], root / "main-neutral-house.house-removed.png"), "House output linkage differs")
    images["main-neutral-house.house-removed"], pixel_rows["main-neutral-house.house-removed"] = pixels(root / "main-neutral-house.house-removed.png")
    require(house_report["backend"] == hardware_rows[0]["backend"] and house_report["device"] == hardware_rows[0]["device"] and house_report["driver"] == hardware_rows[0]["driver"], "House device witness differs")
    info = load(root / "main-neutral-house.renderer-info.json")
    require(info["diagnostic_house"] == {"mode": "diagnostic-house-capture", "report": house_report_path.name, "complete": True,
            "report_sha256": digest(house_report_path), "house_id": 2, "body_changed_pixels": house_report["pixels"]["changed"], "untouched_startup_visibility": False}, "Main diagnostic renderer report differs")
    house = house_pair(root, house_report, sources["building.house.ready"], material, neutral, images, Path(report["accepted_build_root"]))
    tree(root, [*ledger["native_output_before_ledger"], {"path": str(ledger_path), "relative_path": ledger_path.name,
                "bytes": ledger_path.stat().st_size, "sha256": digest(ledger_path)}])
    used = sum(path.stat().st_size for path in files(root).values()); storage = ledger["storage"]
    require(used <= LIMIT and storage == {"maximum_output_bytes": LIMIT, "minimum_free_bytes": HEADROOM,
            "minimum_observed_free_bytes": storage["minimum_observed_free_bytes"], "output_bytes_before_ledger": used-ledger_path.stat().st_size,
            "aggregate_limit_claimed": False, "bulk_art_copied": False, "failed_roots_retained": True} and
            type(storage["minimum_observed_free_bytes"]) is int and storage["minimum_observed_free_bytes"] >= HEADROOM, "Native budget/headroom differs")
    require(ledger["claims"] == {"ordinary_startup_captures": True, "instrumented_house_visibility": True, "untouched_startup_visibility": False,
            "fps": None, "gpu_execution_ms": None, "default_package": False, "production_neutral_art": False, "reference_fidelity": False, "full_engine": False}, "Main scope overstates delivery")
    require(changed(images["main-menu"], images["main-material-menu"]) == 0, "Cold optional collection changed ordinary menu")
    differences = {name: changed(images["main-menu"], images[name]) for name in ("main-world", "main-material-world")}
    require(all(count > 1000 for count in differences.values()), "World versus menu is vacuous")
    differences["ordinary_vs_material_world"] = changed(images["main-world"], images["main-material-world"])
    return {"hardware": hardware_rows[0], "pixels": pixel_rows, "world_scene_pixels": scenes, "menu_world_changed_pixels": differences,
            "house": house, "process_count": 5, "capture_count": 6, "profile_count": 5, "file_count": 34, "output_bytes": used,
            "scope": "actual ordinary main plus instrumented authority-built House; elapsed FX/UI varies; no untouched video/input/FPS/reference acceptance"}


def pair_negative_controls(house_report: dict) -> list[dict]:
    # The command-only relation is independently checked without renderer calls.
    full, removed = house_report["body"]["full_frame"], house_report["body"]["removed_frame"]
    index = house_report["body"]["command_index"]
    result = []
    for name, key in (("changed-vertex", "vertices"), ("changed-light", "lighting"), ("missing-other-command", "commands")):
        altered = copy.deepcopy(removed)
        if key == "vertices": altered[key][0][0] += 1.
        elif key == "lighting": altered[key][0][0] += 1.
        else: del altered[key][0]
        try:
            command_only_removal(full, altered, index)
        except AssertionError:
            pass
        else:
            raise AssertionError("Malformed paired-state control was accepted")
        result.append({"id": name, "rejected": True, "scope": "in-memory exact single-command relation; no native execution"})
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("main_build_root", "material_root", "positive_first_root", "positive_second_root", "cold_first_root", "cold_second_root", "first_native_root", "second_native_root"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args(); names = tuple(vars(args)); paths = {name: getattr(args, name) for name in names}
    require(os.name == "nt" and len({os.path.normcase(os.path.abspath(path)) for path in paths.values()}) == len(paths), "Eight distinct Windows roots required")
    for path in paths.values():
        no_reparse(path); require(path.is_dir() and same(path.parent, REPOSITORY / "build"), "Evidence root must be an existing immediate build child")
    roots = tuple(paths[name] for name in ("positive_first_root", "positive_second_root", "cold_first_root", "cold_second_root"))
    original_reader = REPOSITORY / "tests/gameview_neutral_material_main_evidence_tests.py"
    previous_reader = REPOSITORY / "tests/gameview_neutral_material_main_evidence_review_tests.py"
    review_reader = Path(__file__).absolute()
    require(review_reader.name == "gameview_neutral_material_main_admission_review_tests.py" and
            not same(review_reader, original_reader) and not same(review_reader, previous_reader), "Separate post-build main reader required")
    require(original_reader.is_file() and original_reader.stat().st_size == ORIGINAL_MAIN_READER_BYTES and
            digest(original_reader) == ORIGINAL_MAIN_READER_SHA, "Original compiled main reader identity differs")
    require(previous_reader.is_file() and previous_reader.stat().st_size == PREVIOUS_MAIN_REVIEW_READER_BYTES and
            digest(previous_reader) == PREVIOUS_MAIN_REVIEW_READER_SHA, "Previous post-build main reader identity differs")
    guarded = [review_reader, original_reader, previous_reader, REPOSITORY / "tests/gameview_neutral_material_evidence_tests.py",
               REPOSITORY / "tests/gameview_neutral_material_evidence_review_tests.py",
               paths["main_build_root"] / "main-build-report.json", *[root / "native-execution.json" for root in roots],
               *[root / "neutral-material-report.json" for root in roots], *[paths[name] / "main-native-execution.json" for name in ("first_native_root", "second_native_root")]]
    initial = [(str(path), path.stat().st_size, digest(path)) for path in guarded]
    report, accepted, required, sources, neutral = main_build(paths["main_build_root"], paths["material_root"], roots)
    qualification = qualify_scene_crop(Path(report["accepted_build_root"])); negatives = scene_negative_controls()
    results = [native(paths[name], paths["main_build_root"], paths["material_root"], roots, report, accepted, required, sources, neutral)
               for name in ("first_native_root", "second_native_root")]
    require(results[0]["hardware"] == results[1]["hardware"], "Hardware changed between main repeats")
    for name in ("main-menu", "main-material-menu"):
        require(results[0]["pixels"][name]["rgba_sha256"] == results[1]["pixels"][name]["rgba_sha256"], "Ordinary menu repeat differs")
    # Simulated construction/source/pivot/carrier identity is deterministic. Actual
    # complete frames and PNGs use real elapsed FX/UI; compare their contribution
    # separately within each run and never strip differing frame/capture fields.
    stable = ("construction", "source_record_sha256", "generated_rgba_sha256", "bounds", "actual_material", "uv_contract", "qualified_pixels")
    require({key: results[0]["house"][key] for key in stable} == {key: results[1]["house"][key] for key in stable}, "Real construction/source/body semantics differ")
    pair_negatives = pair_negative_controls(load(paths["first_native_root"] / "main-neutral-house.house-diagnostic.json"))
    require(initial == [(str(path), path.stat().st_size, digest(path)) for path in guarded], "Read-only verifier mutated guarded evidence")
    print(json.dumps({"complete": True, "actual_process_count": 10, "actual_capture_count": 12, "scene_qualification": qualification,
        "scene_negative_controls": negatives, "pair_negative_controls": pair_negatives, "first": results[0], "second": results[1]}, ensure_ascii=False, sort_keys=True))
    print("Neutral main capture evidence checks passed")


if __name__ == "__main__":
    main()
