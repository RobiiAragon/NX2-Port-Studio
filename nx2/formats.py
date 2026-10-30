# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded readers for decrypted NPDM and NSO files. No keys or firmware input."""
from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path


class DumpError(ValueError):
    """An incomplete, unsupported or damaged input, safe to show to the user."""


def region(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise DumpError(f"{label}: range exceeds the file or containing block")
    return data[offset:offset + size]


def read_npdm(path: Path) -> dict:
    if path.stat().st_size > 16 * 1024 * 1024:
        raise DumpError("main.npdm exceeds the 16 MiB metadata limit")
    data = path.read_bytes()
    if len(data) < 0x80 or data[:4] != b"META":
        raise DumpError("main.npdm: missing or truncated META header")
    aci_off, aci_size, acid_off, acid_size = struct.unpack_from("<4I", data, 0x70)
    if aci_off < 0x80 or acid_off < 0x80:
        raise DumpError("main.npdm: access blocks overlap the META header")
    aci = region(data, aci_off, aci_size, "ACI0")
    acid = region(data, acid_off, acid_size, "ACID")
    if len(aci) < 0x40 or aci[:4] != b"ACI0":
        raise DumpError("main.npdm: missing or truncated ACI0 block")
    if max(aci_off, acid_off) < min(aci_off + aci_size, acid_off + acid_size):
        raise DumpError("main.npdm: ACI0 and ACID blocks overlap")
    # Eden NX2 identifies the new layout by the ACID magic after a 0x300-byte
    # signature/key area; this reads metadata, it does not verify signatures.
    if len(acid) >= 0x340 and acid[0x300:0x304] == b"ACID":
        platform = "switch2"
    elif len(acid) >= 0x240 and acid[0x200:0x204] == b"ACID":
        platform = "switch1"
    else:
        raise DumpError("main.npdm: unrecognized ACID layout")
    services = []
    for index, name in enumerate(("filesystem", "services", "capabilities")):
        offset, size = struct.unpack_from("<II", aci, 0x20 + index * 8)
        if size and offset < 0x40:
            raise DumpError(f"ACI0 {name}: overlaps its header")
        block = region(aci, offset, size, f"ACI0 {name}")
        if name == "services":
            cursor = 0
            while cursor < len(block):
                control = block[cursor]
                length = (control & 7) + 1
                raw = region(block, cursor + 1, length, "service name")
                try:
                    text = raw.decode("ascii")
                except UnicodeDecodeError as error:
                    raise DumpError("main.npdm: non-ASCII service name") from error
                if not all(0x21 <= byte <= 0x7e for byte in raw):
                    raise DumpError("main.npdm: invalid service name")
                services.append({"name": text, "register": bool(control & 0x80)})
                cursor += length + 1
        elif name == "capabilities":
            if len(block) % 4:
                raise DumpError("main.npdm: capability table is not word aligned")
            caps = [word for (word,) in struct.iter_unpack("<I", block)]
    svcs, cores = set(), None
    for cap in caps:
        trailing_ones = ((~cap & 0xffffffff) & ((cap + 1) & 0xffffffff)).bit_length() - 1
        if trailing_ones == 3:
            cores = {"minimum": cap >> 16 & 0xff, "maximum": cap >> 24}
        elif trailing_ones == 4:
            mask, group = cap >> 5 & 0xffffff, cap >> 29
            svcs.update(group * 24 + bit for bit in range(24) if mask >> bit & 1)
    return {
        "platform": platform,
        "program_id": f"{struct.unpack_from('<Q', aci, 0x10)[0]:016X}",
        "is_64_bit": bool(data[0xc] & 1),
        "address_space_type": data[0xc] >> 1 & 7,
        "system_resource_bytes": struct.unpack_from("<I", data, 0x14)[0],
        "main_stack_bytes": struct.unpack_from("<I", data, 0x1c)[0],
        "main_priority": data[0xe], "main_core": data[0xf],
        "core_range": cores, "services": services, "svcs": sorted(svcs),
        "signature_verified": False,
    }


MODULE_NAME = re.compile(r"(?:rtld|main|sdk|subsdk(?:[0-9]|[12][0-9]|3[01]))\Z")
MAX_SEGMENT = 512 * 1024 * 1024


def read_nso(path: Path, *, census: bool = False) -> dict:
    size = path.stat().st_size
    with path.open("rb") as source:
        header = source.read(0x100)
        if len(header) != 0x100 or header[:4] != b"NSO0":
            raise DumpError(f"{path.name}: missing or truncated NSO0 header")
        flags = struct.unpack_from("<I", header, 0xc)[0]
        segments, file_ranges, memory_ranges = [], [], []
        cpu = {"SVC": 0, "RETAA": 0, "RETAB": 0, "PACIASP": 0,
               "PACIBSP": 0, "AUTIASP": 0, "AUTIBSP": 0}
        fixed = {0xd65f0bff: "RETAA", 0xd65f0fff: "RETAB", 0xd503233f: "PACIASP",
                 0xd503237f: "PACIBSP", 0xd50323bf: "AUTIASP", 0xd50323ff: "AUTIBSP"}
        for index, name in enumerate(("text", "ro", "data")):
            offset, address, unpacked = struct.unpack_from("<III", header, 0x10 + 0x10 * index)
            stored = struct.unpack_from("<I", header, 0x60 + 4 * index)[0]
            compressed = bool(flags >> index & 1)
            if unpacked > MAX_SEGMENT:
                raise DumpError(f"{path.name}: {name} exceeds the 512 MiB segment limit")
            if stored > MAX_SEGMENT + 8 * 1024 * 1024:
                raise DumpError(f"{path.name}: stored {name} exceeds the bounded reader limit")
            if not compressed and stored != unpacked:
                raise DumpError(f"{path.name}: {name} has inconsistent uncompressed size")
            if stored and (offset < 0x100 or offset > size or stored > size - offset):
                raise DumpError(f"{path.name}: {name} is truncated or overlaps the header")
            if unpacked and address + unpacked > 0x100000000:
                raise DumpError(f"{path.name}: {name} overflows module address space")
            for start, end in file_ranges:
                if stored and max(offset, start) < min(offset + stored, end):
                    raise DumpError(f"{path.name}: stored segments overlap")
            for start, end in memory_ranges:
                if unpacked and max(address, start) < min(address + unpacked, end):
                    raise DumpError(f"{path.name}: memory segments overlap")
            file_ranges.append((offset, offset + stored))
            memory_ranges.append((address, address + unpacked))
            source.seek(offset)
            raw = source.read(stored)
            if compressed:
                if not stored and unpacked:
                    raise DumpError(f"{path.name}: empty compressed {name} segment")
                try:
                    import lz4.block
                except ImportError as error:
                    raise DumpError("Compressed NSO validation needs lz4: install requirements-nx2.txt") from error
                try:
                    raw = lz4.block.decompress(raw, uncompressed_size=unpacked) if unpacked else b""
                except Exception as error:
                    raise DumpError(f"{path.name}: invalid LZ4 {name} segment") from error
            if len(raw) != unpacked:
                raise DumpError(f"{path.name}: {name} decompressed to the wrong size")
            checked = bool(flags >> (3 + index) & 1)
            if checked and hashlib.sha256(raw).digest() != header[0xa0 + 32 * index:0xc0 + 32 * index]:
                raise DumpError(f"{path.name}: {name} SHA-256 mismatch; finish or repeat the dump")
            if census and index == 0:
                if len(raw) % 4:
                    raise DumpError(f"{path.name}: text is not ARM64 word aligned")
                for (word,) in struct.iter_unpack("<I", raw):
                    if word in fixed:
                        cpu[fixed[word]] += 1
                    elif word & 0xffe0001f == 0xd4000001:
                        cpu["SVC"] += 1
            segments.append({"name": name, "file_offset": offset, "memory_offset": address,
                             "stored_bytes": stored, "memory_bytes": unpacked,
                             "compressed": compressed, "hash_verified": checked})
    return {"name": path.name, "file_bytes": size, "build_id": header[0x40:0x60].hex(),
            "bss_bytes": struct.unpack_from("<I", header, 0x3c)[0], "segments": segments,
            "cpu_census": cpu if census else None,
            "cpu_census_scope": "word patterns in text, may include embedded data" if census else None}
