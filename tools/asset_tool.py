"""Build a deterministic, licensed asset registry and reject broken references."""
import argparse
import hashlib
import json
import posixpath
import re
import struct
from pathlib import Path, PurePosixPath
from html.parser import HTMLParser

KINDS = {".ttf": "font", ".png": "texture", ".rml": "ui", ".rcss": "style", ".json": "data", ".rtex": "data", ".dxil": "shader"}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)


def confined(root, relative):
    path = PurePosixPath(relative)
    if path.is_absolute() or ".." in path.parts or "\\" in relative or ":" in relative:
        raise ValueError(f"Invalid asset path: {relative}")
    resolved = (root / relative).resolve()
    if not resolved.is_relative_to(root.resolve()):
        raise ValueError(f"Asset symlink escapes root: {relative}")
    return resolved


class References(HTMLParser):
    def __init__(self):
        super().__init__()
        self.files = []

    def handle_starttag(self, tag, attributes):
        for key, value in attributes:
            if key in ("src", "href") and value and not value.startswith("#"):
                self.files.append(value)


def build(project, assets):
    metadata = read_json(project / "content/asset_sources.json")
    records = []
    seen = set()
    candidates = []
    for path in sorted(assets.rglob("*")):
        if not path.is_file() or path == assets / "manifest.json":
            continue
        relative = path.relative_to(assets).as_posix()
        confined(assets, relative)
        if relative.casefold() in seen:
            raise ValueError(f"Case-colliding asset path: {relative}")
        seen.add(relative.casefold())
        candidates.append((path, relative))
    # Preflight the complete namespace before looking up case-sensitive metadata.
    # Otherwise Linux can report an unowned uppercase group before its collision.
    for path, relative in candidates:
        kind = KINDS.get(path.suffix)
        if kind is None:
            raise ValueError(f"Unrecognized asset format: {relative}")
        entry = metadata["overrides"].get(relative)
        if entry is None:
            if relative.split("/", 1)[0] not in metadata["project_groups"]:
                raise ValueError(f"Asset has no source/license metadata: {relative}")
            entry = {"id": "project:" + relative, "license": "LicenseRef-Proprietary", "author": metadata["project_author"],
                     "source": "project", "notice": metadata["project_notice"]}
        for field in ("id", "license", "author", "source", "notice"):
            if not isinstance(entry.get(field), str) or not entry[field]:
                raise ValueError(f"Missing {field} metadata: {relative}")
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+-]*", entry["license"]):
            raise ValueError(f"Invalid license identifier: {relative}")
        notice = confined(project, entry["notice"])
        if not notice.is_file() or not notice.stat().st_size:
            raise ValueError(f"Missing license notice: {relative}")
        data = path.read_bytes()
        if len(data) > 64 * 1024 * 1024:
            raise ValueError(f"Asset exceeds 64 MiB: {relative}")
        record = dict(entry, path=relative, kind=kind, sha256=hashlib.sha256(data).hexdigest(), bytes=len(data))
        if kind == "texture":
            if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
                raise ValueError(f"Invalid PNG: {relative}")
            width, height = struct.unpack(">II", data[16:24])
            if not 0 < width <= 16384 or not 0 < height <= 16384:
                raise ValueError(f"PNG dimensions out of range: {relative}")
            record.update(width=width, height=height)
        if path.suffix == ".json":
            read_json(path)
        records.append(record)
    ids = [record["id"] for record in records]
    if len(set(ids)) != len(ids):
        raise ValueError("Duplicate stable asset ID")
    paths = {record["path"] for record in records}
    for record in records:
        path = assets / record["path"]
        if path.suffix not in (".rml", ".rcss"):
            continue
        text = path.read_text(encoding="utf-8")
        references = []
        if path.suffix == ".rml":
            parser = References()
            parser.feed(text)
            references += parser.files
        references += re.findall(r"url\(\s*['\"]?([^)'\"\s]+)", text)
        for reference in references:
            relative = posixpath.normpath(posixpath.join(str(PurePosixPath(record["path"]).parent), reference))
            confined(assets, relative)
            if relative not in paths:
                raise ValueError(f"Broken asset reference in {record['path']}: {reference}")
    catalogs = [read_json(assets / "locales" / f"{locale}.json") for locale in ("en", "uk", "ru", "pl", "es")]
    base = set(catalogs[0])
    if not base:
        raise ValueError("Empty English localization catalog")
    for catalog in catalogs:
        if set(catalog) != base or any(not isinstance(value, str) or not value for value in catalog.values()):
            raise ValueError("Localization keys or string types differ from English")
    records.sort(key=lambda record: record["id"])
    fields = ("id", "path", "kind", "sha256", "license", "source", "author", "notice")
    canonical = b"".join(b"".join(str(len(record[field].encode("utf-8"))).encode("ascii") + b":" + record[field].encode("utf-8")
                                for field in fields) + b"\n" for record in records)
    return {"schema": 1, "content_sha256": hashlib.sha256(canonical).hexdigest(), "assets": records}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    parser.add_argument("assets", type=Path)
    args = parser.parse_args()
    result = build(args.project.resolve(), args.assets.resolve())
    (args.assets / "manifest.json").write_text(json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Validated {len(result['assets'])} assets, license notices, references and five locale catalogs.")


if __name__ == "__main__":
    main()
