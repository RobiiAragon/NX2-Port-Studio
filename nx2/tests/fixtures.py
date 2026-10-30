"""Original synthetic structures, built at test time. No game or SDK bytes."""
import hashlib
import struct
from pathlib import Path


def npdm(*, switch2=True, program_id=0x0400C3F00006E000):
    aci = bytearray(0x60)
    aci[:4] = b"ACI0"
    struct.pack_into("<Q", aci, 0x10, program_id)
    struct.pack_into("<6I", aci, 0x20, 0x40, 0, 0x40, 4, 0x44, 4)
    aci[0x40:0x44] = b"\x02gsv"
    struct.pack_into("<I", aci, 0x44, 7 | 28 << 4 | 59 << 10 | 5 << 24)
    signature = 0x300 if switch2 else 0x200
    acid = bytearray(signature + 0x40)
    acid[signature:signature + 4] = b"ACID"
    data = bytearray(0x80)
    data[:4] = b"META"
    data[0xc] = 7
    struct.pack_into("<I", data, 0x14, 0x1000000)
    struct.pack_into("<4I", data, 0x70, 0x80, len(aci), 0x80 + len(aci), len(acid))
    return data + aci + acid


def nso(*, compressed=False, text=None, ro=None):
    text = text if text is not None else struct.pack("<4I", 0xd503201f, 0xd65f0fff, 0xd4000021, 0xd65f03c0)
    segments = [text, ro if ro is not None else b"original-synthetic-ro", b"synthetic-data"]
    data = bytearray(0x100)
    data[:4] = b"NSO0"
    struct.pack_into("<I", data, 0xc, 0x38 | (7 if compressed else 0) | (0x80 if compressed == "zbic" else 0))
    data[0x40:0x60] = bytes(range(32))
    for index, raw in enumerate(segments):
        if compressed == "zbic":
            import ctypes
            from nx2.compression import library
            buffer = ctypes.create_string_buffer(len(raw) * 2 + 256)
            size = library().nx2_zbic_encode(raw, len(raw), buffer, len(buffer))
            if size < 0:
                raise ValueError("Synthetic ZBIC encoding failed")
            stored = buffer.raw[:size]
        elif compressed:
            import lz4.block
            stored = lz4.block.compress(raw, store_size=False)
        else:
            stored = raw
        struct.pack_into("<III", data, 0x10 + index * 0x10, len(data), index * 0x1000, len(raw))
        struct.pack_into("<I", data, 0x60 + index * 4, len(stored))
        data[0xa0 + index * 32:0xc0 + index * 32] = hashlib.sha256(raw).digest()
        data.extend(stored)
    return data


def romfs_image():
    header = struct.pack("<10Q", 0x50, 0x50, 4, 0x54, 0x18, 0x6c, 4, 0x70, 0, 0x70)
    root = struct.pack("<6I", 0, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0)
    return header + struct.pack("<I", 0) + root + struct.pack("<I", 0xffffffff)


def dump(root: Path, *, compressed=False):
    exefs = root / "exefs"
    exefs.mkdir(parents=True)
    (exefs / "main.npdm").write_bytes(npdm())
    for name in ("main", "rtld", "sdk", "subsdk0"):
        (exefs / name).write_bytes(nso(compressed=compressed))
    (root / "romfs").mkdir()
    (root / "romfs" / "original-test.txt").write_text("Synthetic input, not game content.")
    return root
