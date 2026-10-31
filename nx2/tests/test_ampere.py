import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from nx2.pipeline import REPOSITORY

TOOL = Path(os.environ.get("NX2_AMPERE_TOOL", REPOSITORY / ".nx2-build/native/nx2-ampere-lower"))


@unittest.skipUnless(TOOL.is_file(), "build native tool first: make -C native")
class AmpereTests(unittest.TestCase):
    def run_tool(self, words, *, preexisting=False, graphics=False, stage=1):
        with tempfile.TemporaryDirectory(prefix="nx2-shader-") as temp:
            source, output = Path(temp) / "input.bin", Path(temp) / "output.bin"
            header = struct.pack("<32I", (stage << 10), *range(1, 32)) if graphics else b""
            original = header + struct.pack("<" + "I" * len(words), *words)
            source.write_bytes(original)
            if preexisting:
                output.write_bytes(b"preserve-this")
            result = subprocess.run([str(TOOL), *(["--graphics"] if graphics else []), str(source), str(output)], capture_output=True, text=True)
            self.assertEqual(source.read_bytes(), original, "the source file must remain unchanged")
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

    def test_graphics_header_and_scheduling_preserve_branch_destinations(self):
        # The branch crosses scheduling at byte 112 (relative 32 after SPH).
        # Native instruction 4 must become byte 136. The adapter's entry prefix
        # aligns Eden's schedule and every body PC with Ryujinx's decoder.
        words = [0x7947, 48, 0x03800000, 0] + [0x7918, 0, 0, 0] * 3 + [0x794d, 0, 0x03800000, 0]
        result, output = self.run_tool(words, graphics=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(output[:80], struct.pack("<20I", 1 << 10, *range(1, 20)))
        self.assertEqual(struct.unpack_from("<Q", output, 80)[0], 0)
        self.assertEqual(struct.unpack_from("<Q", output, 88)[0], 0x50b0000000070f00)
        self.assertEqual(struct.unpack_from("<Q", output, 112)[0], 0)
        branch = struct.unpack_from("<Q", output, 96)[0]
        displacement = (branch >> 20) & 0xffffff
        self.assertEqual(96 + 8 + displacement, 136)
        self.assertEqual(struct.unpack_from("<Q", output, 136)[0], 0xe30000000007000f)
        self.assertEqual(output[144:], bytes(8))
        self.assertEqual(json.loads(result.stdout)["format"], "maxwell-graphics-ryujinx-prefixed-scheduling")

    def test_fragment_header_is_accepted_but_other_stages_are_rejected(self):
        for stage in (1, 5):
            result, output = self.run_tool([0x794d, 0, 0x03800000, 0], graphics=True, stage=stage)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(output), 112)
        for stage in (0, 2, 3, 4, 6, 7):
            result, output = self.run_tool([0x794d, 0, 0x03800000, 0], graphics=True, stage=stage)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("only vertex and fragment", result.stderr)
            self.assertIsNone(output)

    def test_uniform_forward_branch_preserves_target_across_scheduling_word(self):
        words = [0x7947, 49, 0x03800000, 0] + [0x7918, 0, 0, 0] * 3 + [0x794d, 0, 0x03800000, 0]
        result, output = self.run_tool(words, graphics=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        branch = struct.unpack_from("<Q", output, 96)[0]
        self.assertEqual(96 + 8 + ((branch >> 20) & 0xffffff), 136)
        self.assertEqual(struct.unpack_from("<Q", output, 112)[0], 0)
        self.assertEqual(struct.unpack_from("<Q", output, 136)[0], 0xe30000000007000f)
        self.assertEqual(json.loads(result.stdout)["normalized_uniform_branches"], 1)

    def test_uniform_backward_predicated_branch_preserves_signed_destination(self):
        # Native instruction 4 branches back to instruction 1: -64 bytes from
        # the next native PC. Bit 32 is .U, independent of the signed offset.
        words = [0x7918, 0, 0, 0] * 4 + [0x0947, 0xffffffc1, 0x0383ffff, 0] + [0x794d, 0, 0x03800000, 0]
        result, output = self.run_tool(words, graphics=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        branch = struct.unpack_from("<Q", output, 136)[0]
        displacement = (branch >> 20) & 0xffffff
        if displacement & 0x800000:
            displacement -= 0x1000000
        self.assertEqual(136 + 8 + displacement, 104)
        self.assertEqual(branch >> 16 & 15, 0)  # Original P0 predicate retained.
        self.assertEqual(struct.unpack_from("<Q", output, 112)[0], 0)
        self.assertEqual(json.loads(result.stdout)["normalized_uniform_branches"], 1)

    def test_uniform_hint_normalization_does_not_touch_other_opcodes(self):
        result, output = self.run_tool([0x7802, 1, 0x00000f00, 0, 0x794d, 0, 0x03800000, 0])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["normalized_uniform_branches"], 0)
        self.assertEqual(struct.unpack_from("<Q", output)[0] >> 20 & 0xffffffff, 1)

    def test_reserved_branch_bit_fails_without_output_and_preserves_existing_output(self):
        for immediate in (2, 3):
            for preexisting in (False, True):
                result, output = self.run_tool([0x7947, immediate, 0x03800000, 0,
                                                0x794d, 0, 0x03800000, 0], preexisting=preexisting)
                self.assertEqual(result.returncode, 3)
                self.assertIn("reserved BRA bit 33", result.stderr)
                self.assertEqual(output, b"preserve-this" if preexisting else None)

    def test_graphics_calls_require_translator_continuation_integration(self):
        # A valid native NOINC call and matching link-register RET lower to CAL
        # and RET, but the locked decoder drops the caller's continuation.
        words = [0x00377802, 32, 0xf00, 0,
                 0x7944, 48, 0x03c00000, 0,
                 0x7802, 0x12345678, 0xf00, 0,
                 0xff007387, 0, 0x00100800, 0,
                 0x794d, 0, 0x03800000, 0,
                 0x37007950, 0xffffffa0, 0x03c3ffff, 0]
        for preexisting in (False, True):
            result, output = self.run_tool(words, graphics=True, preexisting=preexisting)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("continuation integration", result.stderr)
            self.assertEqual(output, b"preserve-this" if preexisting else None)

    @unittest.skipUnless(shutil.which("dotnet") and
                         (REPOSITORY / "dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate.dll").is_file(),
                         "the pinned translator and dotnet are needed for the semantic fixture")
    def test_pinned_translator_preserves_entry_spill_and_uniform_branch(self):
        # An observable private-memory write proves that the decoder executed
        # the entry MOV rather than dropping it as a scheduling word. A second
        # program branches across a scheduling boundary before the same write.
        marker = 0x12345678
        programs = {
            "entry": [0x7802, marker, 0xf00, 0,
                      0xff007387, 0, 0x00100800, 0,
                      0xff017983, 0, 0x00300800, 0,
                      0xff007322, 0x7001, 0xff, 0,
                      0x794d, 0, 0x03800000, 0],
            "branch": [0x7802, marker, 0xf00, 0, 0x7947, 49, 0x03800000, 0] +
                      [0x7802, 0xdeadbeef, 0xf00, 0] * 3 +
                      [0xff007387, 0, 0x00100800, 0,
                       0xff017983, 0, 0x00300800, 0,
                       0xff007322, 0x7001, 0xff, 0,
                       0x794d, 0, 0x03800000, 0],
        }
        with tempfile.TemporaryDirectory(prefix="nx2-xlate-") as directory:
            root = Path(directory)
            jobs = []
            for name, words in programs.items():
                header = bytearray(128)
                struct.pack_into("<II", header, 0, 1 << 10, 16)  # 16 bytes per invocation.
                native, maxwell = root / (name + ".nvn2"), root / (name + ".maxwell")
                native.write_bytes(header + struct.pack("<" + "I" * len(words), *words))
                lower = subprocess.run([str(TOOL), "--graphics", str(native), str(maxwell)],
                                       capture_output=True, text=True, timeout=30)
                self.assertEqual(lower.returncode, 0, lower.stderr)
                jobs.append({"key": name, "stage": "vs", "code": str(maxwell)})
            job_file = root / "jobs.json"
            job_file.write_text(json.dumps(jobs))
            translated = subprocess.run(["dotnet", str(REPOSITORY / "dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate.dll"),
                                         str(job_file), str(root / "spv")], capture_output=True, text=True, timeout=30)
            self.assertEqual(translated.returncode, 0, translated.stderr)
            for name in programs:
                data = (root / "spv" / (name + ".spv")).read_bytes()
                words = struct.unpack("<" + "I" * (len(data) // 4), data)
                constants, stores, private_pointers = set(), [], set()
                position = 5
                while position < len(words):
                    count, opcode = words[position] >> 16, words[position] & 0xffff
                    self.assertGreater(count, 0)
                    instruction = words[position:position + count]
                    if opcode == 43 and count == 4 and instruction[3] == marker:  # OpConstant
                        constants.add(instruction[2])
                    if opcode == 32 and instruction[2] == 6:  # OpTypePointer Private
                        private_pointers.add(instruction[1])
                    if opcode == 62:  # OpStore
                        stores.append(instruction[2])
                    position += count
                self.assertTrue(private_pointers, name)
                self.assertTrue(constants.intersection(stores),
                                f"{name}: entry/branch result must reach a private-memory store")
                resources = json.loads((root / "spv" / (name + ".res.json")).read_text())
                self.assertEqual(resources["sbufs"], [], "local spills must not bind a scratch pool")

    def test_graphics_header_without_code_produces_no_output(self):
        result, output = self.run_tool([], graphics=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIsNone(output)

    def test_graphics_existing_output_and_computed_branches_remain_guarded(self):
        result, output = self.run_tool([0x794d, 0, 0x03800000, 0], graphics=True, preexisting=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output, b"preserve-this")
        result, output = self.run_tool([0x7949, 0, 0, 0], graphics=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("address-map", result.stderr)
        self.assertIsNone(output)


if __name__ == "__main__":
    unittest.main()
