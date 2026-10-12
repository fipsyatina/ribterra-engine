import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "asset_tool.py"
SPEC = importlib.util.spec_from_file_location("ribterra_asset_tool", MODULE_PATH)
asset_tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(asset_tool)


class Fixture:
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ribterra-asset-tool-")
        self.project = Path(self.temp.name)
        self.assets = self.project / "assets"
        (self.project / "content").mkdir()
        (self.project / "NOTICE.txt").write_text("Fixture-owned assets.\n", encoding="utf-8")
        self.metadata = {
            "project_author": "Ribterra tests",
            "project_notice": "NOTICE.txt",
            "project_groups": ["ui", "locales"],
            "overrides": {},
        }
        self.write_metadata()
        self.write("ui/main.rml", '<rml><head><link href="style.rcss"/></head><body/></rml>')
        self.write("ui/style.rcss", "body { color: white; }")
        for locale in ("en", "uk", "ru", "pl", "es"):
            self.write(f"locales/{locale}.json", json.dumps({"hello": f"hello-{locale}"}))

    def write(self, relative, value):
        path = self.assets / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value, encoding="utf-8", newline="\n")
        return path

    def write_metadata(self):
        (self.project / "content" / "asset_sources.json").write_text(
            json.dumps(self.metadata), encoding="utf-8", newline="\n")

    def close(self):
        self.temp.cleanup()


class AssetToolTests(unittest.TestCase):
    def setUp(self):
        self.fixture = Fixture()

    def tearDown(self):
        self.fixture.close()

    def test_manifest_is_stable_and_sorted(self):
        first = asset_tool.build(self.fixture.project, self.fixture.assets)
        second = asset_tool.build(self.fixture.project, self.fixture.assets)
        self.assertEqual(first, second)
        self.assertEqual([row["id"] for row in first["assets"]], sorted(row["id"] for row in first["assets"]))
        self.assertEqual(7, len(first["assets"]))

    def test_broken_rml_reference_is_rejected(self):
        self.fixture.write("ui/main.rml", '<rml><head><link href="missing.rcss"/></head></rml>')
        with self.assertRaisesRegex(ValueError, "Broken asset reference"):
            asset_tool.build(self.fixture.project, self.fixture.assets)
        self.fixture.write("ui/main.rml", "<rml><body/></rml>")
        self.fixture.write("ui/style.rcss", "body { background-image: url(missing.png); }")
        with self.assertRaisesRegex(ValueError, "Broken asset reference"):
            asset_tool.build(self.fixture.project, self.fixture.assets)

    def test_missing_and_escaping_license_notices_are_rejected(self):
        self.fixture.metadata["overrides"]["ui/main.rml"] = {
            "id": "ui:main", "license": "MIT", "author": "test", "source": "fixture", "notice": "missing.txt"
        }
        self.fixture.write_metadata()
        with self.assertRaisesRegex(ValueError, "Missing license notice"):
            asset_tool.build(self.fixture.project, self.fixture.assets)
        self.fixture.metadata["overrides"]["ui/main.rml"]["notice"] = "../outside.txt"
        self.fixture.write_metadata()
        with self.assertRaisesRegex(ValueError, "Invalid asset path"):
            asset_tool.build(self.fixture.project, self.fixture.assets)

    def test_locale_key_and_value_parity_is_required(self):
        self.fixture.write("locales/es.json", json.dumps({"different": "hola"}))
        with self.assertRaisesRegex(ValueError, "Localization keys"):
            asset_tool.build(self.fixture.project, self.fixture.assets)
        self.fixture.write("locales/es.json", json.dumps({"hello": ""}))
        with self.assertRaisesRegex(ValueError, "Localization keys"):
            asset_tool.build(self.fixture.project, self.fixture.assets)

    def test_duplicate_ids_and_unknown_metadata_groups_are_rejected(self):
        self.fixture.metadata["overrides"] = {
            "ui/main.rml": {"id": "same", "license": "MIT", "author": "test", "source": "fixture", "notice": "NOTICE.txt"},
            "ui/style.rcss": {"id": "same", "license": "MIT", "author": "test", "source": "fixture", "notice": "NOTICE.txt"},
        }
        self.fixture.write_metadata()
        with self.assertRaisesRegex(ValueError, "Duplicate stable asset ID"):
            asset_tool.build(self.fixture.project, self.fixture.assets)
        self.fixture.metadata["overrides"] = {}
        self.fixture.write_metadata()
        self.fixture.write("unowned/data.json", "{}")
        with self.assertRaisesRegex(ValueError, "no source/license metadata"):
            asset_tool.build(self.fixture.project, self.fixture.assets)

    def test_malformed_license_identifier_is_rejected(self):
        self.fixture.metadata["overrides"]["ui/main.rml"] = {
            "id": "ui:main", "license": "unknown license text", "author": "test",
            "source": "fixture", "notice": "NOTICE.txt",
        }
        self.fixture.write_metadata()
        with self.assertRaisesRegex(ValueError, "Invalid license"):
            asset_tool.build(self.fixture.project, self.fixture.assets)

    def test_case_collisions_and_symlink_escape_where_supported(self):
        upper = self.fixture.assets / "UI"
        try:
            upper.mkdir()
            (upper / "main.rml").write_text("<rml/>", encoding="utf-8")
        except FileExistsError:
            pass  # Case-insensitive filesystems cannot contain the invalid package.
        else:
            with self.assertRaisesRegex(ValueError, "Case-colliding"):
                asset_tool.build(self.fixture.project, self.fixture.assets)
            (upper / "main.rml").unlink()
            upper.rmdir()
        outside = self.fixture.project / "outside.json"
        outside.write_text("{}", encoding="utf-8")
        link = self.fixture.assets / "ui" / "escape.json"
        try:
            os.symlink(outside, link)
        except (OSError, NotImplementedError):
            return
        with self.assertRaisesRegex(ValueError, "symlink escapes root"):
            asset_tool.build(self.fixture.project, self.fixture.assets)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
