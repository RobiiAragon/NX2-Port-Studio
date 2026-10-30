# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded binding to the separate ZBIC-enabled native library."""
import ctypes
import sys
from functools import lru_cache
from pathlib import Path


@lru_cache(maxsize=1)
def library():
    root = Path(__file__).resolve().parents[1]
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    candidates = [root / "native" / ("libnx2_zbic" + suffix),
                  root / ".nx2-build/native" / ("libnx2_zbic" + suffix)]
    path = next((p for p in candidates if p.is_file()), None)
    if path is None:
        raise RuntimeError("ZBIC decoder not built: run make -C native, or use the packaged NX2 app")
    lib = ctypes.CDLL(str(path))
    for name in ("nx2_zbic_decode", "nx2_zbic_encode"):
        function = getattr(lib, name)
        function.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
        function.restype = ctypes.c_longlong
    return lib


def decompress(raw: bytes, expected: int) -> bytearray:
    if not 0 < expected <= 512 * 1024 * 1024:
        raise ValueError("ZBIC output size is outside the supported range")
    output = bytearray(expected)
    view = (ctypes.c_ubyte * expected).from_buffer(output)
    count = library().nx2_zbic_decode(raw, len(raw), view, expected)
    if count != expected:
        raise ValueError("invalid ZBIC frame or decompressed size mismatch")
    return output
