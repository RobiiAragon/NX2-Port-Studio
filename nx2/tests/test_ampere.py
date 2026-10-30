import json
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from nx2.pipeline import REPOSITORY

TOOL = Path(os.environ.get("NX2_AMPERE_TOOL", REPOSITORY / ".nx2-build/native/nx2-ampere-lower"))


@unittest.skipUnless(TOOL.is_file(), "build native tool first: make -C native")
class AmpereTests(unittest.TestCase):
    def run_tool(self, words, *, preexisting=False):
        with tempfile.TemporaryDirectory(prefix="nx2-shader-") as temp:
            source, output = Path(temp) / "input.bin", Path(temp) / "output.bin"
            source.write_bytes(struct.pack("<" + "I" * len(words), *words))
            if preexisting:
                output.write_bytes(b"preserve-this")
            result = subprocess.run([str(TOOL), str(source), str(output)], capture_output=True, text=True)
            return result, output.read_bytes() if output.exists() else None

    def test_exit_lowers_to_expected_maxwell_instruction(self):
        result, output = self.run_tool([0x794d, 0, 0x03800000, 0])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(struct.unpack("<Q", output)[0], 0xe30000000007000f)
        self.assertFalse(json.loads(result.stdout)["metal_ready"])

    def test_unknown_opcode_produces_no_output(self):
        result, output = self.run_tool([0x7fff, 0, 0, 0])
        self.assertEqual(result.returncode, 3)
        self.assertIn("unsupported", result.stderr)
        self.assertIsNone(output)

    def test_missing_terminator_and_unaligned_code_produce_no_output(self):
        for words in ([0x7fff], [0x7918, 0, 0, 0]):
            result, output = self.run_tool(words)
            self.assertNotEqual(result.returncode, 0)
            self.assertIsNone(output)

    def test_no_terminator_is_explicitly_diagnosed(self):
        result, output = self.run_tool([0x7918, 0, 0, 0])
        self.assertEqual(result.returncode, 3)
        self.assertIn("no unconditional EXIT", result.stderr)
        self.assertIsNone(output)

    def test_noop_then_exit_keeps_control_flow_end(self):
        result, output = self.run_tool([0x7918, 0, 0, 0, 0x794d, 0, 0x03800000, 0])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["source_bytes"], 32)
        self.assertEqual(struct.unpack("<2Q", output), (0x50b0000000070f00, 0xe30000000007000f))

    def test_direct_branch_to_exit_is_relocated(self):
        result, output = self.run_tool([0x7947, 0, 0x03800000, 0, 0x794d, 0, 0x03800000, 0])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(struct.unpack("<2Q", output), (0xe24000000007000f, 0xe30000000007000f))

    def test_computed_branch_requires_address_map_integration(self):
        result, output = self.run_tool([0x7949, 0, 0, 0])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("address-map", result.stderr)
        self.assertIsNone(output)

    def test_existing_output_is_preserved(self):
        result, output = self.run_tool([0x794d, 0, 0x03800000, 0], preexisting=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output, b"preserve-this")


if __name__ == "__main__":
    unittest.main()
