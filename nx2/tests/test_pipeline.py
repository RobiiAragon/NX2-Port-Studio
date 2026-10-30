import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from nx2.formats import DumpError, read_npdm, read_nso
from nx2.pipeline import REPOSITORY, inspect_dump, prepare
from nx2.tests.fixtures import dump, npdm, nso, romfs_image


class DumpTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nx2-test-")
        self.base = Path(self.temp.name)
        self.root = dump(self.base / "game with spaces")

    def tearDown(self):
        self.temp.cleanup()

    def test_valid_dump_from_root_exefs_and_main(self):
        for path in (self.root, self.root / "exefs", self.root / "exefs/main"):
            report = inspect_dump(path, census=True)
            self.assertEqual(report["metadata"]["program_id"], "0400C3F00006E000")
            self.assertEqual(report["metadata"]["system_resource_bytes"], 0x1000000)
            self.assertEqual(report["metadata"]["core_range"], {"minimum": 0, "maximum": 5})
            self.assertFalse(report["requires_keys"])
            self.assertFalse(report["runtime_ready"])
            self.assertEqual(len(report["modules"]), 4)
            self.assertEqual(report["modules"][0]["cpu_census"]["RETAB"], 1)
            self.assertEqual(report["modules"][0]["cpu_census"]["SVC"], 1)

    def test_switch1_is_not_misidentified_as_switch2(self):
        path = self.root / "exefs/main.npdm"
        path.write_bytes(npdm(switch2=False))
        self.assertEqual(read_npdm(path)["platform"], "switch1")
        with self.assertRaisesRegex(DumpError, "not a 64-bit Switch 2"):
            inspect_dump(self.root)

    def test_bananza_identity_does_not_claim_runtime_compatibility(self):
        (self.root / "exefs/main.npdm").write_bytes(npdm(program_id=0x040074A01BF12000))
        report = inspect_dump(self.root)
        self.assertEqual(report["target"]["name"], "Donkey Kong Bananza")
        self.assertFalse(report["target"]["gameplay_verified"])
        self.assertFalse(report["runtime_ready"])

    def test_missing_module_and_empty_romfs(self):
        sdk = self.root / "exefs/sdk"
        content = sdk.read_bytes()
        sdk.unlink()
        with self.assertRaisesRegex(DumpError, "missing sdk"):
            inspect_dump(self.root)
        sdk.write_bytes(content)
        (self.root / "romfs/original-test.txt").unlink()
        with self.assertRaisesRegex(DumpError, "RomFS is empty"):
            inspect_dump(self.root)

    def test_hash_corruption_is_not_reported_as_ready(self):
        path = self.root / "exefs/main"
        raw = bytearray(path.read_bytes())
        raw[-1] ^= 1
        path.write_bytes(raw)
        with self.assertRaisesRegex(DumpError, "SHA-256 mismatch"):
            inspect_dump(self.root)

    def test_truncated_nso_and_overlapping_segments(self):
        path = self.root / "exefs/main"
        good = nso()
        path.write_bytes(good[:0x101])
        with self.assertRaisesRegex(DumpError, "truncated"):
            read_nso(path)
        broken = bytearray(good)
        struct.pack_into("<I", broken, 0x20, 0x100)
        path.write_bytes(broken)
        with self.assertRaisesRegex(DumpError, "overlap"):
            read_nso(path)

    def test_npdm_bounds_and_service_table(self):
        path = self.root / "exefs/main.npdm"
        for offset, value in ((0x70, 0xfffffff0), (0x74, 0xffffffff)):
            bad = npdm()
            struct.pack_into("<I", bad, offset, value)
            path.write_bytes(bad)
            with self.assertRaises(DumpError):
                read_npdm(path)
        bad = npdm()
        bad[0xc0] = 7  # eight-byte name cannot fit the four-byte service table
        path.write_bytes(bad)
        with self.assertRaisesRegex(DumpError, "service name"):
            read_npdm(path)

    def test_symlink_input_and_module_are_rejected(self):
        link = self.base / "linked-game"
        link.symlink_to(self.root, target_is_directory=True)
        with self.assertRaisesRegex(DumpError, "symbolic link"):
            inspect_dump(link)
        main = self.root / "exefs/main"
        main.unlink()
        main.symlink_to(self.root / "exefs/sdk")
        with self.assertRaisesRegex(DumpError, "regular file"):
            inspect_dump(self.root)

    def test_romfs_image_and_ambiguous_names(self):
        import shutil
        shutil.rmtree(self.root / "romfs")
        image = romfs_image()
        (self.root / "RomFS.img").write_bytes(image)
        self.assertEqual(inspect_dump(self.root)["input"]["romfs_kind"], "image")
        (self.root / "second.romfs").write_bytes(image)
        with self.assertRaisesRegex(DumpError, "one RomFS image"):
            inspect_dump(self.root)

    def test_prepare_records_builds_without_copying_games_or_claiming_exports(self):
        output = self.base / "private workspace"
        result = prepare(self.root, output, platform="iOS")
        self.assertFalse(result["runtime_ready"])
        plan = json.loads((output / "build-plan.json").read_text())
        self.assertIsNone(plan["artifact"])
        self.assertEqual(plan["architecture"], "arm64")
        self.assertIn("main", plan["module_build_ids"])
        self.assertEqual(len(list(output.iterdir())), 4)
        self.assertFalse(any(p.suffix == ".keys" for p in output.iterdir()))
        with self.assertRaisesRegex(DumpError, "already exists"):
            prepare(self.root, output, platform="macOS")

    def test_prepare_rejects_source_dump_and_dangling_output_links(self):
        for output in (REPOSITORY / ".nx2-build/private", self.root / "private"):
            output.parent.mkdir(exist_ok=True)
            with self.assertRaisesRegex(DumpError, "outside"):
                prepare(self.root, output, platform="macOS")
        output = self.base / "dangling"
        output.symlink_to(self.base / "missing")
        with self.assertRaisesRegex(DumpError, "already exists"):
            prepare(self.root, output, platform="macOS")
        self.assertTrue(output.is_symlink())

    def test_cli_json_error_and_success_without_keys_or_firmware(self):
        command = [sys.executable, str(REPOSITORY / "scripts/nx2.py"), "inspect"]
        for source, expected in ((self.root, 0), (self.base / "missing", 1)):
            result = subprocess.run(command + [str(source), "--json"], capture_output=True, text=True)
            self.assertEqual(result.returncode, expected, result.stderr)
            value = json.loads(result.stdout)
            self.assertEqual(value["stage"], "validated-input" if expected == 0 else "error")

    @unittest.skipUnless(importlib.util.find_spec("lz4"), "lz4 not installed")
    def test_compressed_segments_are_decompressed_and_verified(self):
        compressed = dump(self.base / "compressed", compressed=True)
        report = inspect_dump(compressed)
        self.assertTrue(report["modules"][0]["segments"][0]["compressed"])
        self.assertTrue(report["modules"][0]["segments"][0]["hash_verified"])


if __name__ == "__main__":
    unittest.main()
