import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from nx2.errors import DumpError
from nx2.pipeline import prepare
from nx2.resources import import_catalog, inspect_catalog, inspect_registered
from nx2.tests.fixtures import dump


class ResourcesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.catalog = self.base / "source"
        self.fonts = self.catalog / "system_data/0100000000000811/files"
        self.fonts.mkdir(parents=True)
        self.content = b"original-synthetic-font-fixture"
        (self.fonts / "synthetic.bfttf").write_bytes(self.content)
        self.manifest = {"formatVersion": 1, "firmwareVersion": "23.0.1", "resources": [{
            "titleId": "0100000000000811", "relativePath": "system_data/0100000000000811/files",
            "files": [{"path": "synthetic.bfttf", "bytes": len(self.content),
                       "sha256": hashlib.sha256(self.content).hexdigest()}]}, {
            "titleId": "010000000000000e", "relativePath": "not-even-present",
            "files": [{"path": "not-read.keyring"}]}]}
        self.save()

    def save(self):
        (self.catalog / "manifest.json").write_text(json.dumps(self.manifest))

    def tearDown(self):
        self.temp.cleanup()

    def test_only_mii_and_fonts_are_candidates_without_claiming_compatibility(self):
        report = inspect_catalog(self.catalog, source_platform="switch1")
        self.assertEqual(report["file_count"], 1)
        self.assertEqual(report["source_platform"], "switch1")
        self.assertEqual(report["switch2_compatibility"], "unverified")
        self.assertFalse(report["runtime_enabled"])

    def test_import_is_private_versioned_and_keeps_source(self):
        library = self.base / "nx2-resources"
        result = import_catalog(self.catalog, library, source_platform="switch1")
        self.assertEqual(library.stat().st_mode & 0o777, 0o700)
        registered = inspect_registered(library)
        self.assertEqual(result["content_digest"], registered["content_digest"])
        self.assertEqual((self.fonts / "synthetic.bfttf").read_bytes(), self.content)
        self.assertFalse((library / "system_data/010000000000000e").exists())
        with self.assertRaisesRegex(DumpError, "already exists"):
            import_catalog(self.catalog, library, source_platform="switch1")

    def test_hash_mismatch_and_path_traversal_are_rejected(self):
        item = self.manifest["resources"][0]["files"][0]
        item["sha256"] = "0" * 64
        self.save()
        with self.assertRaisesRegex(DumpError, "SHA-256"):
            inspect_catalog(self.catalog, source_platform="switch1")
        item["path"] = "../secret.bfttf"
        self.save()
        with self.assertRaises(DumpError):
            inspect_catalog(self.catalog, source_platform="switch1")

    def test_registered_tampering_and_unregistered_catalog_are_rejected(self):
        with self.assertRaisesRegex(DumpError, "registered with NX2"):
            inspect_registered(self.catalog)
        library = self.base / "registered"
        import_catalog(self.catalog, library, source_platform="switch1")
        font = library / "system_data/0100000000000811/files/synthetic.bfttf"
        font.write_bytes(b"x" * len(self.content))
        with self.assertRaisesRegex(DumpError, "SHA-256"):
            inspect_registered(library)

    def test_optional_resources_are_recorded_but_not_required_by_workspaces(self):
        game = dump(self.base / "game")
        library = self.base / "registered"
        import_catalog(self.catalog, library, source_platform="switch1")
        output = self.base / "workspace"
        prepare(game, output, platform="macOS", resources=library)
        plan = json.loads((output / "build-plan.json").read_text())
        self.assertEqual(plan["resources"]["source_platform"], "switch1")
        self.assertFalse(plan["resources"]["runtime_enabled"])
        prepare(game, self.base / "without-resources", platform="macOS")


if __name__ == "__main__":
    unittest.main()
