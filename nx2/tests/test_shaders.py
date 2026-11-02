# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import struct
import unittest
from unittest.mock import patch

from nx2.errors import DumpError
from nx2.shaders import read_bnsh, read_control, read_sead_primitive, read_shader, read_spirv


def control(stage=0, code_size=144, *, source=None):
    data = bytearray(0xD8)
    struct.pack_into("<III", data, 0, 0x98761234, 201, 11)
    struct.pack_into("<I", data, 0x38, code_size)
    data[0x56] = stage
    struct.pack_into("<I", data, 0xBC, 0xD8)
    if source is not None:
        compiler = bytearray(0x50)
        struct.pack_into("<III", compiler, 0, 0x37F20897, 201, 11)
        struct.pack_into("<I", compiler, 0x2C, 0x48)
        struct.pack_into("<II", compiler, 0x48, len(source), stage)
        compiler += source
        struct.pack_into("<I", data, 0xC0, len(compiler))
        data += compiler
    struct.pack_into("<I", data, 0x30, len(data))
    return data


def code(stage=1):
    data = bytearray(144)
    struct.pack_into("<I", data, 0, stage << 10)
    struct.pack_into("<4I", data, 128, 0x794D, 0, 0x03800000, 0)
    return data


def spirv(model=0):
    # Authored framing fixture, not a semantically complete shader module.
    return struct.pack("<11I", 0x07230203, 0x00010600, 0, 2, 0,
                       (6 << 16) | 15, model, 1, 0x6E69616D, 0, 1)


def bnsh():
    data = bytearray(0x900)
    data[:8] = b"BNSH\0\0\0\0"
    struct.pack_into("<I", data, 8, 0x00020400)
    data[12:14] = b"\xff\xfe"
    struct.pack_into("<I", data, 0x1C, len(data))
    data[0x60:0x64] = b"grsc"
    struct.pack_into("<IQ", data, 0x7C, 2, 0xC0)
    # Both variations share one program, as serialized containers commonly do.
    struct.pack_into("<Q", data, 0xD0, 0x140)
    struct.pack_into("<Q", data, 0x110, 0x140)
    for slot, stage, sph, descriptor, ctl, native in ((0, 0, 1, 0x200, 0x300, 0x600),
                                                   (4, 1, 5, 0x240, 0x400, 0x700)):
        struct.pack_into("<Q", data, 0x148 + slot * 8, descriptor)
        struct.pack_into("<QQII", data, descriptor + 8, ctl, native, 256, 0xD8)
        data[ctl:ctl + 0xD8] = control(stage)
        data[native:native + 144] = code(sph)
    return data


class ShaderTests(unittest.TestCase):
    def test_declared_control_and_code_lengths_exclude_allocation_padding(self):
        ctl, native = control(), code()
        # Adjacent independent fields must never become the high bits of u64 sizes.
        struct.pack_into("<I", ctl, 0x34, 0xFEED)
        struct.pack_into("<I", ctl, 0x3C, 0xBEEF)
        shader = read_shader(ctl + b"outer-control-padding", native + bytes(112))
        self.assertEqual(shader.control.data, ctl)
        self.assertEqual(shader.code, native)
        self.assertEqual(shader.storage_bytes, 256)
        self.assertEqual(shader.key, hashlib.sha256(ctl + native).hexdigest()[:16])
        self.assertIsNone(shader.control.source_spirv)

    def test_unknown_control_versions_and_stages_fail_explicitly(self):
        for offset, value in ((4, 1), (8, 20), (0x56, 8)):
            data = control()
            if offset == 0x56:
                data[offset] = value
            else:
                struct.pack_into("<I", data, offset, value)
            with self.assertRaises(DumpError) as error:
                read_control(data)
            self.assertEqual(error.exception.code, "unsupported-shader-format")

    def test_truncated_or_overlapping_control_regions_fail(self):
        for offset, value in ((0x30, 0xD4), (0x30, 0x1000), (0x38, 0),
                              (0xBC, 0), (0xBC, 0x1000), (0xC0, 4)):
            data = control()
            struct.pack_into("<I", data, offset, value)
            with self.assertRaises(DumpError):
                read_control(data)
        with self.assertRaises(DumpError):
            read_control(control()[:0xD7])

    def test_stage_checks_use_independent_api_container_and_sph_enums(self):
        self.assertEqual(read_shader(control(1), code(5)).control.stage, "ps")
        for ctl, native, expected in ((control(1), code(1), None),
                                      (control(1), code(5), "vs"),
                                      (control(0, 128), code(), None),
                                      (control(0, 145), code() + b"x", None)):
            with self.assertRaises(DumpError):
                read_shader(ctl, native, expected_stage=expected)

    def test_optional_compiler_source_is_framed_and_stage_checked(self):
        module = spirv()
        parsed = read_control(control(source=module))
        self.assertEqual(parsed.source_spirv.data, module)
        self.assertEqual(parsed.source_spirv.entry_points[0].name, "main")
        self.assertEqual(parsed.source_spirv.entry_points[0].stage, "vs")
        with self.assertRaises(DumpError):
            read_control(control(source=spirv(4)))
        for offset, value in ((0xD8, 0), (0xD8 + 0x2C, 0),
                              (0xD8 + 0x48, 0x10000), (0xD8 + 0x4C, 1)):
            malformed = control(source=module)
            struct.pack_into("<I", malformed, offset, value)
            with self.assertRaises(DumpError):
                read_control(malformed)

    def test_spirv_bad_instruction_lengths_and_header_fail(self):
        for data in (spirv()[:-1], spirv()[:20], bytes(20)):
            with self.assertRaises(DumpError):
                read_spirv(data)
        for offset, value in ((4, 0x00010700), (12, 0), (20, 15), (20, (99 << 16) | 15),
                              (24, 5267), (28, 2), (32, 0xFFFFFFFF), (36, 0xFFFFFFFF)):
            malformed = bytearray(spirv())
            struct.pack_into("<I", malformed, offset, value)
            with self.assertRaises(DumpError):
                read_spirv(malformed)

    def test_bnsh_relative_pointers_padding_and_reused_descriptors(self):
        data = bnsh()
        variations = read_bnsh(b"prefix" + data + b"trailing outer data", 6)
        self.assertEqual(len(variations), 2)
        self.assertEqual([s.control.stage for s in variations[0].shaders], ["vs", "ps"])
        self.assertIs(variations[0].shaders[0], variations[1].shaders[0])
        self.assertEqual(variations[0].shaders[0].storage_bytes, 256)

    def test_bnsh_file_and_aggregate_payload_limits(self):
        data = bnsh()
        with patch("nx2.shaders.MAX_BNSH_BYTES", len(data) - 1):
            with self.assertRaisesRegex(DumpError, "file exceeds"):
                read_bnsh(data)
        # Two distinct descriptors each reserve 216 control + 256 code bytes.
        # Reusing those descriptors in a second variation consumes no more.
        with patch("nx2.shaders.MAX_BNSH_SHADER_BYTES", 944):
            self.assertEqual(len(read_bnsh(data)), 2)
        with patch("nx2.shaders.MAX_BNSH_SHADER_BYTES", 943):
            with self.assertRaisesRegex(DumpError, "memory limit"):
                read_bnsh(data)

    def test_compute_code_framing_still_requires_complete_instructions(self):
        with self.assertRaisesRegex(DumpError, "16-byte aligned"):
            read_shader(control(5, 15), bytes(15))

    def test_bnsh_bad_counts_pointers_stage_slots_and_extensions_fail(self):
        for offset, form, value in ((8, "I", 0x00020301), (0x1C, "I", 0xFFFF),
                                     (0x7C, "I", 1_000_001), (0x80, "Q", 0),
                                     (0xD0, "Q", 0xFFF), (0x148, "Q", 0xFFF),
                                     (0x148 + 8, "Q", 0x200), (0x140 + 0x38, "Q", 0x280),
                                     (0x200 + 8, "Q", 0xFFF), (0x200 + 24, "I", 128)):
            data = bnsh()
            struct.pack_into("<" + form, data, offset, value)
            with self.assertRaises(DumpError):
                read_bnsh(data)

    def test_sead_primitive_explicit_offsets_and_overlap_rejection(self):
        data = bytearray(0x700)
        struct.pack_into("<4I", data, 0, 0x10, 0x100, 0x300, 0x400)
        data[0x10:0xE8] = control()
        data[0x100:0x1D8] = control(1)
        data[0x300:0x390] = code()
        data[0x400:0x490] = code(5)
        self.assertEqual([s.control.stage for s in read_sead_primitive(data)], ["vs", "ps"])
        for field, value in ((0, 0), (8, 0x20), (12, 0x6FF)):
            malformed = bytearray(data)
            struct.pack_into("<I", malformed, field, value)
            with self.assertRaises(DumpError):
                read_sead_primitive(malformed)


if __name__ == "__main__":
    unittest.main()
