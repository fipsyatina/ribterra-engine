"""Exercise candidate validation and retention of a previous asset directory."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SOURCE, SHADERS, LOCALIZATION_CHECK, UI_CHECK = map(Path, sys.argv[1:])


class AssetStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ribterra-staging-test-")
        self.project = Path(self.temp.name)
        for folder in ("assets", "content", "licenses"):
            shutil.copytree(SOURCE / folder, self.project / folder)
        self.binary = self.project / "build" / "runtime"
        self.assets = self.binary / "assets"
        self.assets.mkdir(parents=True)
        (self.assets / "previous.txt").write_text("previous valid generation", encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def stage(self, preserve=False):
        # Test both modes explicitly, independently of the developer's chosen
        # retention policy for the real build directory.
        environment = dict(os.environ, RIBTERRA_PRESERVE_BUILD_ARTIFACTS="1" if preserve else "0")
        return subprocess.run([sys.executable, str(SOURCE / "tools/stage_assets.py"), str(self.project),
                               str(self.binary), str(SHADERS), str(LOCALIZATION_CHECK), str(UI_CHECK)],
                              capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60,
                              env=environment)

    def assert_previous_retained(self):
        self.assertEqual((self.assets / "previous.txt").read_text(encoding="utf-8"), "previous valid generation")
        self.assertFalse(list(self.binary.glob(".ribterra-assets-*")))

    def test_bad_icu_candidate_retains_previous_assets(self):
        path = self.project / "assets/locales/pl.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data["hud.population"] = "{count,plural,one{broken}"
        path.write_text(json.dumps(data), encoding="utf-8")
        result = self.stage()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Invalid ICU pattern", result.stdout + result.stderr)
        self.assert_previous_retained()

    def test_missing_native_ui_control_retains_previous_assets(self):
        path = self.project / "assets/ui/menu.rml"
        data = path.read_text(encoding="utf-8")
        self.assertIn('id="new-game"', data)
        path.write_text(data.replace('id="new-game"', 'id="missing-new-game"'), encoding="utf-8")
        result = self.stage()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Missing menu element", result.stdout + result.stderr)
        self.assert_previous_retained()

    def test_valid_candidate_replaces_previous_assets(self):
        result = self.stage()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.assets / "manifest.json").is_file())
        self.assertFalse((self.assets / "previous.txt").exists())
        self.assertFalse(list(self.binary.glob(".ribterra-assets-*")))

    def test_preserve_mode_keeps_previous_generation_after_success(self):
        result = self.stage(preserve=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.assets / "manifest.json").is_file())
        retained = list(self.binary.glob(".ribterra-assets-retained-*"))
        self.assertEqual(len(retained), 1)
        self.assertEqual((retained[0] / "previous/previous.txt").read_text(encoding="utf-8"),
                         "previous valid generation")
        self.assertFalse((retained[0] / "current").exists())

    def test_preserve_mode_keeps_rejected_candidate_and_previous_generation(self):
        path = self.project / "assets/locales/pl.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        data["hud.population"] = "{count,plural,one{broken}"
        path.write_text(json.dumps(data), encoding="utf-8")
        result = self.stage(preserve=True)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Invalid ICU pattern", result.stdout + result.stderr)
        self.assertEqual((self.assets / "previous.txt").read_text(encoding="utf-8"),
                         "previous valid generation")
        retained = list(self.binary.glob(".ribterra-assets-retained-*"))
        self.assertEqual(len(retained), 1)
        self.assertTrue((retained[0] / "current/manifest.json").is_file())
        self.assertFalse((retained[0] / "previous").exists())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
