import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from nx2.errors import DumpError
from nx2.formats import read_nso
from nx2.input_io import stable_file
from nx2.pipeline import REPOSITORY, inspect_dump, prepare, verify_workspace
from nx2.romfs import inspect_image
from nx2.symbols import inspect_linkage
from nx2.tests.fixtures import dump, nso, romfs_image


class HardeningTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.root = dump(self.base / "game")

    def tearDown(self):
        self.temporary.cleanup()

    def test_linkage_distinguishes_missing_required_and_weak_names(self):
        modules = [
            {"name": "main", "symbols": {"status": "read", "imports": ["provided", "required", "optional"],
                                         "exports": [], "weak_imports": ["optional"]}},
            {"name": "sdk", "symbols": {"status": "read", "imports": [],
                                        "exports": ["provided"], "weak_imports": []}},
        ]
        result = inspect_linkage(modules)
        self.assertEqual(result["status"], "names-checked")
        self.assertEqual(result["modules"][0]["required_names_not_found"], ["required"])
        self.assertEqual(result["modules"][0]["weak_names_not_found"], ["optional"])
        self.assertFalse(result["abi_verified"])
        modules[1]["symbols"] = {"status": "not-declared"}
        result = inspect_linkage(modules)
        self.assertEqual(result["status"], "incomplete-tables")
        self.assertEqual(result["uninspected_modules"], ["sdk"])

    def test_uncompressed_size_field_is_not_authoritative(self):
        raw = nso()
        struct.pack_into("<3I", raw, 0x60, 0, 0, 0)
        path = self.root / "exefs/main"
        path.write_bytes(raw)
        self.assertTrue(read_nso(path)["segments"][0]["hash_verified"])

    def test_bounded_zbic_decompression_and_wrong_output_size(self):
        path = self.root / "exefs/main"
        raw = nso(compressed="zbic", text=b"\x1f\x20\x03\xd5" * 100)
        path.write_bytes(raw)
        self.assertEqual(read_nso(path)["segments"][0]["compression"], "zbic")
        struct.pack_into("<I", raw, 0x18, 399)
        path.write_bytes(raw)
        with self.assertRaisesRegex(DumpError, "ZBIC"):
            read_nso(path)

    def test_zbic_trailing_frames_are_refused(self):
        from nx2.compression import decompress
        raw = nso(compressed="zbic")
        offset, size = struct.unpack_from("<I", raw, 0x10)[0], struct.unpack_from("<I", raw, 0x60)[0]
        block = bytes(raw[offset:offset + size])
        with self.assertRaisesRegex(ValueError, "ZBIC"):
            decompress(block + block, 16)

    def test_concurrent_write_and_path_replacement_are_detected(self):
        path = self.base / "changing"
        path.write_bytes(b"before")
        with self.assertRaisesRegex(DumpError, "input changed") as caught:
            with stable_file(path) as (stream, _):
                stream.read()
                path.write_bytes(b"after")
        self.assertEqual(caught.exception.code, "input-changed")
        with self.assertRaisesRegex(DumpError, "input changed"):
            with stable_file(path):
                other = self.base / "replacement"
                other.write_bytes(b"after")
                other.replace(path)

    def test_workspace_rechecks_content_even_without_retail_hash_flags(self):
        path = self.root / "exefs/main"
        raw = nso()
        struct.pack_into("<I", raw, 0xc, 0)
        path.write_bytes(raw)
        output = self.base / "work"
        prepare(self.root, output, platform="macOS")
        self.assertEqual(verify_workspace(output)["stage"], "verified")
        raw[-1] ^= 1
        path.write_bytes(raw)
        with self.assertRaises(DumpError) as caught:
            verify_workspace(output)
        self.assertEqual(caught.exception.code, "input-changed")

    def test_private_workspace_permissions_ignore_permissive_umask(self):
        previous = os.umask(0)
        try:
            output = self.base / "private"
            prepare(self.root, output, platform="macOS")
        finally:
            os.umask(previous)
        self.assertEqual(output.stat().st_mode & 0o777, 0o700)

    def test_romfs_ranges_and_root_are_checked(self):
        path = self.base / "image.romfs"
        good = romfs_image()
        path.write_bytes(good)
        self.assertFalse(inspect_image(path)["file_payloads_verified"])
        for position, value in ((0x18, 0xffffffffffffffff), (0x48, 0xffffffffffffffff), (0x28, 0x54)):
            malformed = bytearray(good)
            struct.pack_into("<Q", malformed, position, value)
            path.write_bytes(malformed)
            with self.assertRaises(DumpError):
                inspect_image(path)

    def test_dynamic_imports_are_read_from_declared_tables(self):
        strings = b"\0nvnBootstrapLoader\0synthetic_export\0"
        table_offset = (len(strings) + 7) & ~7
        table = bytes(24) + struct.pack("<IBBHQQ", 1, 0x12, 0, 0, 0, 0)
        table += struct.pack("<IBBHQQ", 20, 0x12, 0, 1, 0x100, 4)
        raw = nso(ro=strings.ljust(table_offset, b"\0") + table)
        struct.pack_into("<4I", raw, 0x90, 0, len(strings), table_offset, len(table))
        path = self.root / "exefs/main"
        path.write_bytes(raw)
        symbols = read_nso(path, symbols=True)["symbols"]
        self.assertEqual(symbols["imports"], ["nvnBootstrapLoader"])
        self.assertEqual(symbols["exports"], ["synthetic_export"])
        self.assertEqual(symbols["graphics_imports"], ["nvnBootstrapLoader"])
        struct.pack_into("<I", raw, 0x98, 0xffffff00)
        path.write_bytes(raw)
        with self.assertRaises(DumpError) as caught:
            read_nso(path, symbols=True)
        self.assertEqual(caught.exception.code, "invalid-symbols")

    def test_cli_error_has_code_and_recovery_hint(self):
        path = self.root / "exefs/main"
        raw = nso()
        struct.pack_into("<I", raw, 0xc, 0)
        path.write_bytes(raw)
        output = self.base / "workspace"
        prepare(self.root, output, platform="macOS")
        raw[-1] ^= 1
        path.write_bytes(raw)
        result = subprocess.run([sys.executable, str(REPOSITORY / "scripts/nx2.py"), "verify", str(output), "--json"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        message = json.loads(result.stdout)
        self.assertEqual(message["code"], "input-changed")
        self.assertTrue(message["hint"])


if __name__ == "__main__":
    unittest.main()
