import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path


def png(width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    rows = b"".join(b"\0" + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


TOOL_ARGUMENT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None


class AtlasToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = TOOL_ARGUMENT or Path(__file__).resolve().parents[1] / "build/engine-windows/AtlasTool.exe"
        if not cls.tool.is_file():
            raise unittest.SkipTest(f"AtlasTool executable is unavailable: {cls.tool}")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ribterra-atlas-tool-")
        self.root = Path(self.temp.name)
        (self.root / "NOTICE.txt").write_text("Test art notice.\n", encoding="utf-8")
        (self.root / "a.png").write_bytes(png(2, 2, bytes((255, 0, 0, 255)) * 4))
        (self.root / "b.png").write_bytes(png(1, 2, bytes((0, 255, 0, 255)) * 2))
        self.config = {
            "schema": 1, "page_size": 8, "padding": 1, "max_pages": 4,
            "images": [
                {"id": "art:a", "path": "a.png", "pivot": [0.25, 0.75], "license": "MIT", "source": "fixture-a", "author": "test", "notice": "NOTICE.txt"},
                {"id": "art:b", "path": "b.png", "license": "LicenseRef-Proprietary", "source": "fixture-b", "author": "test", "notice": "NOTICE.txt"},
            ],
        }
        self.config_path = self.root / "atlas.json"
        self.write_config()

    def tearDown(self):
        self.temp.cleanup()

    def write_config(self):
        self.config_path.write_text(json.dumps(self.config), encoding="utf-8")

    def run_tool(self, output):
        return subprocess.run([str(self.tool), str(self.config_path), str(output)],
                              text=True, encoding="utf-8", errors="backslashreplace", capture_output=True)

    def test_output_is_deterministic_and_contains_layout_and_provenance(self):
        first, second = self.root / "first", self.root / "second"
        self.assertEqual(0, self.run_tool(first).returncode)
        self.config["images"].reverse(); self.write_config()
        self.assertEqual(0, self.run_tool(second).returncode)
        first_files = {p.relative_to(first).as_posix(): p.read_bytes() for p in first.rglob("*") if p.is_file()}
        second_files = {p.relative_to(second).as_posix(): p.read_bytes() for p in second.rglob("*") if p.is_file()}
        self.assertEqual(first_files, second_files)
        manifest = json.loads((first / "manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(["art:a", "art:b"], [entry["id"] for entry in manifest["entries"]])
        self.assertEqual([0.25, 0.75], manifest["entries"][0]["pivot"])
        self.assertEqual("MIT", manifest["entries"][0]["license"])
        self.assertEqual(64, manifest["pages"][0]["width"] * manifest["pages"][0]["height"])
        self.assertEqual(64, len(manifest["content_sha256"]))
        claimed_hash = manifest.pop("content_sha256")
        canonical = json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
        self.assertEqual(claimed_hash, hashlib.sha256(canonical).hexdigest())
        notice = first / manifest["entries"][0]["notice"]
        self.assertEqual(hashlib.sha256(notice.read_bytes()).hexdigest(), manifest["entries"][0]["notice_sha256"])

    def test_existing_destination_is_retained(self):
        output = self.root / "existing"; output.mkdir(); sentinel = output / "keep.txt"; sentinel.write_text("keep", encoding="utf-8")
        result = self.run_tool(output)
        self.assertNotEqual(0, result.returncode); self.assertEqual("keep", sentinel.read_text(encoding="utf-8"))
        self.assertEqual([], list(self.root.glob("existing.tmp-*")))

    def test_unicode_paths_and_spaces_are_supported(self):
        unicode_root = self.root / "українські матеріали"
        unicode_root.mkdir()
        (unicode_root / "малюнок.png").write_bytes(png(1, 1, bytes((12, 34, 56, 255))))
        (unicode_root / "ЛІЦЕНЗІЯ.txt").write_text("Тестове повідомлення.\n", encoding="utf-8")
        config = {
            "schema": 1, "page_size": 8, "padding": 1, "max_pages": 1,
            "images": [{
                "id": "арт:оселя", "path": "малюнок.png", "license": "MIT",
                "source": "локальний тест", "author": "тест", "notice": "ЛІЦЕНЗІЯ.txt",
            }],
        }
        self.config_path = unicode_root / "опис атласу.json"
        self.config_path.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
        output = unicode_root / "готовий атлас"
        result = self.run_tool(output)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertTrue((output / "atlas-0.png").is_file())
        self.assertEqual("арт:оселя", json.loads((output / "manifest.json").read_text(encoding="utf-8"))["entries"][0]["id"])

    @unittest.skipUnless(sys.platform == "win32", "Windows extended-length path contract")
    def test_output_beyond_legacy_windows_max_path_is_supported(self):
        parent = self.root
        while len(str(parent / "готовий атлас")) <= 280:
            parent /= "long-output-segment-0123456789abcdef"
        parent.mkdir(parents=True)
        output = parent / "готовий атлас"
        self.assertGreater(len(str(output)), 260)
        result = self.run_tool(output)
        self.assertEqual(0, result.returncode, result.stderr)
        manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        notice = output / manifest["entries"][0]["notice"]
        self.assertTrue(notice.is_file())
        self.assertEqual(hashlib.sha256(notice.read_bytes()).hexdigest(),
                         manifest["entries"][0]["notice_sha256"])

    def test_invalid_atlas_options_are_rejected_before_png_decode(self):
        (self.root / "bad.png").write_bytes(b"not png")
        baseline = json.loads(json.dumps(self.config))
        cases = (
            ("page_size", 0x100000008, "uint32 range"),
            ("page_size", -8, "unsigned integer"),
            ("padding", 1.5, "unsigned integer"),
            ("padding", 17, "padding"),
            ("max_pages", 0, "page count"),
        )
        for index, (field, value, message) in enumerate(cases):
            with self.subTest(field=field):
                self.config = json.loads(json.dumps(baseline))
                self.config[field] = value
                self.config["images"][0]["path"] = "bad.png"
                self.write_config()
                output = self.root / f"bad-options-{index}"
                result = self.run_tool(output)
                self.assertNotEqual(0, result.returncode)
                self.assertIn(message, result.stderr.lower())
                self.assertFalse(output.exists())

    def test_notice_changes_are_staged_and_change_manifest_identity(self):
        first, second = self.root / "notice-first", self.root / "notice-second"
        self.assertEqual(0, self.run_tool(first).returncode)
        (self.root / "NOTICE.txt").write_text("Updated test art notice.\n", encoding="utf-8")
        self.assertEqual(0, self.run_tool(second).returncode)
        first_manifest = json.loads((first / "manifest.json").read_text(encoding="utf-8"))
        second_manifest = json.loads((second / "manifest.json").read_text(encoding="utf-8"))
        self.assertNotEqual(first_manifest["content_sha256"], second_manifest["content_sha256"])
        self.assertNotEqual(first_manifest["entries"][0]["notice_sha256"], second_manifest["entries"][0]["notice_sha256"])
        for output, manifest in ((first, first_manifest), (second, second_manifest)):
            notice = output / manifest["entries"][0]["notice"]
            self.assertTrue(notice.is_file())
            self.assertEqual(hashlib.sha256(notice.read_bytes()).hexdigest(), manifest["entries"][0]["notice_sha256"])

    def test_bad_path_png_and_metadata_fail_without_output(self):
        cases = []
        escaped = json.loads(json.dumps(self.config)); escaped["images"][0]["path"] = "../outside.png"; cases.append(escaped)
        broken = json.loads(json.dumps(self.config)); (self.root / "bad.png").write_bytes(b"not png"); broken["images"][0]["path"] = "bad.png"; cases.append(broken)
        metadata = json.loads(json.dumps(self.config)); del metadata["images"][0]["license"]; cases.append(metadata)
        notice = json.loads(json.dumps(self.config)); notice["images"][0]["notice"] = "missing.txt"; cases.append(notice)
        for index, case in enumerate(cases):
            with self.subTest(index=index):
                self.config = case; self.write_config(); output = self.root / f"failed-{index}"
                self.assertNotEqual(0, self.run_tool(output).returncode); self.assertFalse(output.exists())
                self.assertEqual([], list(self.root.glob(f"failed-{index}.tmp-*")))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
