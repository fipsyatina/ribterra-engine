#!/usr/bin/env python3
"""Copy the bounded original menu/HUD asset closure into a fresh diagnostic kit."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import posixpath
import re
import shutil
import stat
import sys
import types
from html.parser import HTMLParser
from pathlib import Path, PurePosixPath

sys.dont_write_bytecode = True

MAX_OUTPUT_BYTES = 128 * 1024 * 1024
MIN_FREE_BYTES = 384 * 1024 * 1024
MAX_ASSET_BYTES = 64 * 1024 * 1024
REPORT_NAME = "ui-kit-report.json"
EXPORTER_PATH = "tools/export_engine_ui_kit.py"
PAYLOAD_PATHS = tuple(sorted((
    "art/world.json", "art/concept-bridge-clay.png", "art/concept-houses-low.png",
    "art/concept-services.png", "art/concept-village.png",
    "fonts/CormorantGaramond.ttf", "fonts/NotoSans-Regular.ttf", "fonts/PTSerif-Regular.ttf",
    "locales/en.json", "locales/es.json", "locales/pl.json", "locales/ru.json", "locales/uk.json",
    "ui/building-panels.rcss", "ui/concept.rcss", "ui/hud-v01.rcss", "ui/hud.rml",
    "ui/menu.rml", "ui/ribterra.rcss", "ui/art/theme.rcss", "ui/art/building-house.png",
    "ui/art/concept-hud-icons.png", "ui/art/concept-panel.png",
    "ui/art/glyph-0.png", "ui/art/glyph-1.png", "ui/art/glyph-2.png", "ui/art/glyph-4.png",
    "ui/art/glyph-5.png", "ui/art/glyph-6.png", "ui/art/glyph-7.png", "ui/art/glyph-8.png",
    "ui/art/glyph-9.png", "ui/art/glyph-10.png", "ui/art/glyph-11.png",
    "ui/art/glyph-12.png", "ui/art/glyph-13.png", "ui/art/icons-cursors.2.png",
    "ui/art/icons-cursors.16.png", "ui/art/icons-cursors.17.png", "ui/art/icons-cursors.19.png",
    "ui/art/lobby-hall.png", "ui/art/lobby-portraits.png", "ui/art/lobby-valley.png",
    "ui/art/menu-landscape.png", "ui/art/ui-kit.0.png", "ui/art/ui-kit.1.png",
    "ui/art/ui-kit.2.png", "ui/art/ui-kit.3.png", "ui/art/ui-kit.4.png", "ui/art/ui-kit.5.png",
)))
NOTICE_PATHS = (
    "licenses/Cormorant-OFL.txt", "licenses/Noto-OFL.txt",
    "licenses/PTSerif-OFL.txt", "licenses/Ribterra-Assets.txt",
)
INPUT_PATHS = tuple(sorted((EXPORTER_PATH, "tools/asset_tool.py", "content/asset_sources.json",
                            *NOTICE_PATHS)))
PORTRAIT_IDS = tuple(sorted(f"building.{name}.ready" for name in (
    "house", "farm", "stockpile", "mine", "workshop", "fishing", "bridge",
    "lumbermill", "mill", "kitchen", "claypit",
)))


class UiKitError(ValueError):
    pass


def portable(relative: str) -> None:
    path = PurePosixPath(relative)
    if (not relative or path.is_absolute() or ".." in path.parts or "\\" in relative or
            ":" in relative or "\0" in relative or path.as_posix() != relative):
        raise UiKitError(f"Noncanonical relative path: {relative!r}")


def no_reparse(path: Path, *, required: bool = True) -> Path:
    """Check lexical ancestors before resolving, including dangling links."""
    absolute = Path(os.path.abspath(path))
    for component in (*reversed(absolute.parents), absolute):
        try:
            info = component.lstat()
        except FileNotFoundError:
            if not required and component == absolute:
                continue
            raise UiKitError(f"Missing path component: {component}") from None
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise UiKitError(f"Reparse/symlink path is forbidden: {component}")
    return absolute.resolve(strict=required)


def contained_file(root: Path, relative: str) -> Path:
    portable(relative)
    path = no_reparse(root / relative)
    if not path.is_relative_to(root) or not path.is_file():
        raise UiKitError(f"Expected a confined regular file: {relative}")
    return path


def hash_row(root: Path, relative: str) -> dict:
    path = contained_file(root, relative)
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(block)
            digest.update(block)
    if path.stat().st_size != size:
        raise UiKitError(f"File changed while hashing: {relative}")
    return {"relative_path": relative, "bytes": size, "sha256": digest.hexdigest()}


def rows(root: Path, relatives: tuple[str, ...] | list[str]) -> list[dict]:
    return [hash_row(root, relative) for relative in sorted(relatives)]


def bound_bytes(root: Path, expected: dict) -> bytes:
    path = contained_file(root, expected["relative_path"])
    if expected["bytes"] > MAX_ASSET_BYTES or path.stat().st_size != expected["bytes"]:
        raise UiKitError(f"Input size changed/exceeds its bound: {expected['relative_path']}")
    with path.open("rb") as stream:
        data = stream.read(expected["bytes"] + 1)
    if len(data) != expected["bytes"] or hashlib.sha256(data).hexdigest() != expected["sha256"]:
        raise UiKitError(f"Input bytes changed: {expected['relative_path']}")
    return data


def headroom(build_root: Path, forthcoming_bytes: int = 0) -> None:
    if shutil.disk_usage(build_root).free < MIN_FREE_BYTES + forthcoming_bytes:
        raise UiKitError("Fresh export must retain at least 384 MiB free headroom")


def tree_files(root: Path) -> list[str]:
    files = []
    for directory, children, names in os.walk(root, followlinks=False):
        no_reparse(Path(directory))
        for child in children:
            checked = no_reparse(Path(directory) / child)
            if not checked.is_relative_to(root) or not checked.is_dir():
                raise UiKitError(f"Invalid output directory: {checked}")
        for name in names:
            path = Path(directory) / name
            relative = path.relative_to(root).as_posix()
            contained_file(root, relative)
            files.append(relative)
    return sorted(files)


class RmlReferences(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.references: list[tuple[str, str]] = []

    def handle_starttag(self, tag, attributes) -> None:
        for key, value in attributes:
            if key in ("src", "href") and value and not value.startswith("#"):
                self.references.append((f"rml.{key}", value))


def reference_closure(root: Path) -> list[dict]:
    result = []
    available = set(PAYLOAD_PATHS)
    url_pattern = re.compile(r"url\(\s*(?:\"([^\"]+)\"|'([^']+)'|([^\s)'\"]+))\s*\)")
    for owner in PAYLOAD_PATHS:
        if Path(owner).suffix not in (".rml", ".rcss"):
            continue
        text = contained_file(root, owner).read_text(encoding="utf-8")
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
        references = []
        if owner.endswith(".rml"):
            parser = RmlReferences()
            parser.feed(text)
            references.extend(parser.references)
        urls = list(url_pattern.finditer(text))
        if len(urls) != len(re.findall(r"url\s*\(", text)):
            raise UiKitError(f"Unsupported or malformed url() in {owner}")
        references.extend(("style.url", next(group for group in match.groups() if group is not None))
                          for match in urls)
        if owner.endswith(".rcss"):
            declarations = re.findall(r"(?<![-\w])src\s*:\s*([^;{}]+);", text)
            if len(declarations) != len(re.findall(r"(?<![-\w])src\s*:", text)):
                raise UiKitError(f"Malformed RCSS src declaration in {owner}")
            for declaration in declarations:
                value = declaration.strip()
                matched_url = url_pattern.fullmatch(value)
                if matched_url:
                    reference = next(group for group in matched_url.groups() if group is not None)
                elif len(value) >= 2 and value[0] in "\"'" and value[-1] == value[0]:
                    reference = value[1:-1]
                elif re.fullmatch(r"[^\s\"'()]+", value):
                    reference = value
                else:
                    raise UiKitError(f"Unsupported RCSS src value in {owner}: {value}")
                references.append(("rcss.src", reference))
        for syntax, reference in references:
            if reference.startswith("/") or "\\" in reference or ":" in reference:
                raise UiKitError(f"External/nonportable UI reference in {owner}: {reference}")
            resolved = posixpath.normpath(posixpath.join(str(PurePosixPath(owner).parent), reference))
            portable(resolved)
            if resolved not in available:
                raise UiKitError(f"Missing UI reference in {owner}: {reference}")
            contained_file(root, resolved)
            result.append({"owner": owner, "syntax": syntax, "reference": reference, "resolved": resolved})
    return sorted(result, key=lambda row: (row["owner"], row["syntax"], row["reference"]))


def protected_paths(repo: Path) -> tuple[str, ...]:
    originals = ["Play Ribterra.cmd", "Play Ribterra Rework.cmd", "dist/current-build.txt"]
    for relative in originals:
        contained_file(repo, relative)
    if os.path.lexists(repo / "Ribterra.exe"):
        launcher = contained_file(repo, "Ribterra.exe")
    else:
        pointer = contained_file(repo, "dist/current-build.txt").read_text(encoding="utf-8-sig").strip()
        if not pointer:
            raise UiKitError("Empty protected current-build pointer")
        launcher = no_reparse(repo / pointer)
    text = contained_file(repo, "Play Ribterra Rework.cmd").read_text(encoding="utf-8-sig")
    match = re.search(r'^set "RIBTERRA_REWORK=([^\"]+\.exe)"\s*$', text, re.MULTILINE | re.IGNORECASE)
    if not match:
        raise UiKitError("Could not resolve protected Rework launcher target")
    rework = no_reparse(repo / match.group(1))
    for path in (launcher, rework):
        if not path.is_relative_to(repo) or not path.is_file():
            raise UiKitError(f"Protected launcher target escapes repository or is absent: {path}")
        originals.append(path.relative_to(repo).as_posix())
    return tuple(sorted(set(originals)))


def portrait_closure(manifest: dict, world: dict) -> list[dict]:
    records = {row["path"]: row for row in manifest["assets"]}
    portraits = []
    for identifier in PORTRAIT_IDS:
        matches = [(page, sprite) for page in world["pages"] for sprite in page["sprites"]
                   if sprite["id"] == identifier]
        if len(matches) != 1:
            raise UiKitError(f"Ready portrait must have exactly one original descriptor: {identifier}")
        page, sprite = matches[0]
        record = records.get(page["path"])
        if not record or record["kind"] != "texture":
            raise UiKitError(f"Ready portrait page missing from UI kit: {identifier}")
        rect = sprite["rect"]
        if (len(rect) != 4 or any(type(value) is not int for value in rect) or
                rect[0] < 0 or rect[1] < 0 or rect[2] <= 0 or rect[3] <= 0 or
                rect[0] + rect[2] > record["width"] or rect[1] + rect[3] > record["height"]):
            raise UiKitError(f"Ready portrait source rectangle is invalid: {identifier}")
        portraits.append({"id": identifier, "page": page["path"], "rect": rect,
                          "alpha_mode": page.get("alpha_mode", "")})
    return portraits


def json_bytes(document: dict) -> bytes:
    return (json.dumps(document, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


def export(repository: Path, destination: Path) -> dict:
    repo = no_reparse(repository)
    if not repo.is_dir() or no_reparse(Path(__file__)) != contained_file(repo, EXPORTER_PATH):
        raise UiKitError("Repository must be the authoritative checkout containing this exporter")
    build_root = no_reparse(repo / "build")
    if not build_root.is_dir():
        raise UiKitError("Authoritative build parent must already exist")
    output = no_reparse(destination, required=False)
    if output.parent != build_root or output == build_root:
        raise UiKitError("Output must be a fresh immediate child of the authoritative build directory")
    if os.path.lexists(output):
        raise UiKitError("Output already exists; exporter refuses to overwrite or clean it")
    if len(PAYLOAD_PATHS) != 50 or len(set(path.casefold() for path in PAYLOAD_PATHS)) != 50:
        raise UiKitError("Exporter payload contract must contain exactly 50 distinct paths")
    source_paths = tuple(f"assets/{relative}" for relative in PAYLOAD_PATHS)
    source_before = rows(repo, source_paths)
    input_before = rows(repo, INPUT_PATHS)
    protected = protected_paths(repo)
    protected_before = rows(repo, protected)
    payload_bytes = sum(row["bytes"] for row in source_before)
    if payload_bytes > MAX_OUTPUT_BYTES or any(row["bytes"] > MAX_ASSET_BYTES for row in source_before):
        raise UiKitError("Original UI payload exceeds the bounded output/asset budget")
    headroom(build_root, payload_bytes)
    os.mkdir(output)
    written = 0

    def write_new(relative: str, data: bytes) -> None:
        nonlocal written
        portable(relative)
        if written + len(data) > MAX_OUTPUT_BYTES:
            raise UiKitError("UI kit exceeds its 128 MiB output budget; failed output retained")
        headroom(build_root, len(data))
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        checked_parent = no_reparse(path.parent)
        if not checked_parent.is_relative_to(output):
            raise UiKitError(f"Output parent escaped fresh root: {relative}")
        no_reparse(path, required=False)
        with path.open("xb") as stream:
            if stream.write(data) != len(data):
                raise UiKitError(f"Incomplete exclusive output write: {relative}")
        written += len(data)
        expected = {"relative_path": relative, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        if hash_row(output, relative) != expected:
            raise UiKitError(f"Copied output bytes changed: {relative}")

    originals = {row["relative_path"]: row for row in source_before}
    for relative in PAYLOAD_PATHS:
        source_relative = f"assets/{relative}"
        expected = originals[source_relative]
        data = bound_bytes(repo, expected)
        write_new(relative, data)
    if tree_files(output) != list(PAYLOAD_PATHS):
        raise UiKitError("Fresh UI kit payload inventory differs from the exact 50-file contract")
    references = reference_closure(output)
    helper_path = contained_file(repo, "tools/asset_tool.py")
    helper_row = next(row for row in input_before if row["relative_path"] == "tools/asset_tool.py")
    helper_data = bound_bytes(repo, helper_row)
    # Execute the source-bound existing helper without reading/writing a pyc.
    asset_tool = types.ModuleType("ribterra_ui_asset_tool")
    asset_tool.__file__ = str(helper_path)
    exec(compile(helper_data, str(helper_path), "exec"), asset_tool.__dict__)
    manifest = asset_tool.build(repo, output)
    if sorted(row["path"] for row in manifest["assets"]) != list(PAYLOAD_PATHS):
        raise UiKitError("Asset tool manifest does not cover exactly the copied UI payload")
    if sorted(set(row["notice"] for row in manifest["assets"])) != sorted(NOTICE_PATHS):
        raise UiKitError("UI manifest notices differ from the original four-notice contract")
    world = asset_tool.read_json(contained_file(output, "art/world.json"))
    portraits = portrait_closure(manifest, world)
    manifest_data = json_bytes(manifest)
    write_new("manifest.json", manifest_data)
    output_paths = sorted((*PAYLOAD_PATHS, "manifest.json"))
    if tree_files(output) != output_paths:
        raise UiKitError("Unexpected file before evidence report publication")
    copy_rows = rows(output, list(PAYLOAD_PATHS))
    for row in copy_rows:
        original = originals[f"assets/{row['relative_path']}"]
        if row["bytes"] != original["bytes"] or row["sha256"] != original["sha256"]:
            raise UiKitError(f"Original/copy mismatch: {row['relative_path']}")
    source_after, input_after, protected_after = rows(repo, source_paths), rows(repo, INPUT_PATHS), rows(repo, protected)
    if source_after != source_before or input_after != input_before or protected_after != protected_before:
        raise UiKitError("Protected sources, helper/metadata/notices or launcher inputs changed during export")
    notice_inputs = {row["relative_path"]: row for row in input_before}
    notices = [{**notice_inputs[relative], "text": bound_bytes(repo, notice_inputs[relative]).decode("utf-8")}
               for relative in NOTICE_PATHS]
    report = {
        "schema": 1, "complete": True, "purpose": "original-menu-hud-diagnostic-subset",
        "repository_root": str(repo), "payload_count": 50, "payload_bytes": payload_bytes,
        "output_file_count": 52, "output_total_bytes": written,
        "limits": {"max_output_bytes": MAX_OUTPUT_BYTES, "min_free_bytes": MIN_FREE_BYTES},
        "sources_before": source_before, "sources_after": source_after,
        "copies": [{**row, "source_relative_path": f"assets/{row['relative_path']}"} for row in copy_rows],
        "inputs_before": input_before, "inputs_after": input_after,
        "protected_before": protected_before, "protected_after": protected_after,
        "notices": notices, "references": references,
        "rcss_src_references": [row for row in references if row["syntax"] == "rcss.src"],
        "manifest": {**hash_row(output, "manifest.json"), "asset_count": 50,
                     "content_sha256": manifest["content_sha256"]},
        "output_inventory_excluding_report": rows(output, output_paths),
        "report_not_registered_as_asset": True, "report_filename": REPORT_NAME,
        "ready_portraits": portraits,
        "scope": {"menu_hud": True, "ready_portrait_count": 11, "complete_world_catalog": False,
                  "research_level2_other_buildings": "OPEN", "normal_startup": "OPEN",
                  "full_engine": "OPEN", "reference_fidelity": "NOT_ACCEPTED", "fps": None},
    }
    # Include the report's byte count without a self-hash or output-path variation.
    for _ in range(8):
        report_data = json_bytes(report)
        total = written + len(report_data)
        if report["output_total_bytes"] == total:
            break
        report["output_total_bytes"] = total
    else:
        raise UiKitError("Report size did not stabilize; failed output retained")
    write_new(REPORT_NAME, report_data)
    if written != report["output_total_bytes"] or tree_files(output) != sorted((*output_paths, REPORT_NAME)):
        raise UiKitError("Final UI kit output inventory/byte count changed")
    if rows(repo, source_paths) != source_before or rows(repo, INPUT_PATHS) != input_before or rows(repo, protected) != protected_before:
        raise UiKitError("Final source/protected binding changed; failed output retained")
    if rows(output, output_paths) != report["output_inventory_excluding_report"]:
        raise UiKitError("Final copied assets/manifest changed; failed output retained")
    headroom(build_root)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("repository", type=Path)
    parser.add_argument("fresh_output", type=Path)
    arguments = parser.parse_args()
    try:
        report = export(arguments.repository, arguments.fresh_output)
    except (ValueError, OSError, KeyError, TypeError, UnicodeError) as error:
        print(f"UI kit export failed; all existing/failed output files retained: {error}", file=sys.stderr)
        return 2
    print(f"Exported {report['payload_count']} original assets / {report['output_file_count']} files / "
          f"{report['output_total_bytes']} bytes; source and protected inputs unchanged.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
