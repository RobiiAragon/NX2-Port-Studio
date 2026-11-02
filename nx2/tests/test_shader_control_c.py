# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual runtime metadata reader against authored CPU fixtures."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(shutil.which("cc"), "a C compiler is needed for the runtime control fixture")
class RuntimeControlTests(unittest.TestCase):
    def test_bounded_runtime_reader(self):
        repository = Path(__file__).resolve().parents[2]
        source = r'''
#include <assert.h>
#include <stdio.h>
#include "dm/gfx/nvn/nvn2_shader_control.h"
static void u32(unsigned char *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (8 * i));
}
static void fixture(unsigned char *p) {
    memset(p, 0, 0xe0);
    u32(p, 0x98761234); u32(p + 4, 201); u32(p + 8, 11);
    u32(p + 0x30, 0xd8); u32(p + 0x38, 144); u32(p + 0xbc, 0xd8);
    // These are independent fields, deliberately nonzero beside the sizes.
    u32(p + 0x34, 0x1234); u32(p + 0x3c, 0x5678);
}
int main(void) {
    unsigned char storage[0xe1], native[145] = {0};
    unsigned char *p = storage + 1; // Force an unaligned input address.
    struct nvn2_ctl_info out, untouched;
    memset(&out, 0xa5, sizeof out); untouched = out;
    fixture(p); u32(native + 1, 1 << 10);
    assert(nvn2_ctl_read(p, 0xd7, &out) == NVN2_CTL_TRUNCATED);
    assert(!memcmp(&out, &untouched, sizeof out));
    assert(nvn2_ctl_read(NULL, 0xe0, &out) == NVN2_CTL_TRUNCATED);
    assert(nvn2_ctl_read(p, 0xd8, &out) == NVN2_CTL_OK);
    assert(out.control_bytes == 0xd8 && out.code_bytes == 144 && out.stage == 0);
    assert(nvn2_ctl_check_graphics_code(&out, native + 1, 143) == NVN2_CTL_CODE_RANGE);
    assert(nvn2_ctl_check_graphics_code(&out, NULL, 144) == NVN2_CTL_CODE_RANGE);
    assert(nvn2_ctl_check_graphics_code(&out, native + 1, 144) == NVN2_CTL_OK);
    out.code_bytes = 145;
    assert(nvn2_ctl_check_graphics_code(&out, native + 1, 145) == NVN2_CTL_CODE_RANGE);
    for (unsigned stage = 0; stage < 5; ++stage) {
        static const unsigned char sph[] = {1, 5, 4, 2, 3};
        fixture(p); p[0x56] = (unsigned char)stage;
        assert(nvn2_ctl_read(p, 0xd8, &out) == NVN2_CTL_OK);
        u32(native + 1, sph[stage] << 10);
        assert(nvn2_ctl_check_graphics_code(&out, native + 1, 144) == NVN2_CTL_OK);
        u32(native + 1, 0);
        assert(nvn2_ctl_check_graphics_code(&out, native + 1, 144) == NVN2_CTL_CODE_STAGE);
    }
    fixture(p); u32(p + 8, 20);
    assert(nvn2_ctl_read(p, 0xd8, &out) == NVN2_CTL_VERSION);
    fixture(p); p[0x56] = 8;
    assert(nvn2_ctl_read(p, 0xd8, &out) == NVN2_CTL_STAGE);
    fixture(p); u32(p + 0x30, 0xd4);
    assert(nvn2_ctl_read(p, 0xd8, &out) == NVN2_CTL_SIZE);
    fixture(p); u32(p + 0x30, 0xe0);
    assert(nvn2_ctl_read(p, 0xd8, &out) == NVN2_CTL_TRUNCATED);
    assert(nvn2_ctl_read(p, 0xe0, &out) == NVN2_CTL_OK);
    fixture(p); u32(p + 0xbc, 0xd0);
    assert(nvn2_ctl_read(p, 0xd8, &out) == NVN2_CTL_COMPILER_RANGE);
    fixture(p); u32(p + 0xc0, 0xffffffff);
    assert(nvn2_ctl_read(p, 0xd8, &out) == NVN2_CTL_COMPILER_RANGE);
    fixture(p); u32(p + 0x38, 0);
    assert(nvn2_ctl_read(p, 0xd8, &out) == NVN2_CTL_SIZE);
    puts("NVN2 control fixture passed (CPU metadata only)");
}
'''
        with tempfile.TemporaryDirectory(prefix="nvn2-control-") as directory:
            path = Path(directory)
            fixture, executable = path / "fixture.c", path / "fixture"
            fixture.write_text(source)
            compile_result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                             "-I", str(repository), str(fixture), "-o", str(executable)],
                                            capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("CPU metadata only", result.stdout)


if __name__ == "__main__":
    unittest.main()
