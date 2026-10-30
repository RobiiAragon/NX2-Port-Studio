# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the ranges of a standard decrypted RomFS image, without extraction."""
import hashlib
import struct

from .errors import DumpError
from .input_io import stable_file


def inspect_image(path):
    with stable_file(path) as (source, info):
        header = source.read(0x50)
        if len(header) != 0x50 or struct.unpack_from("<Q", header)[0] != 0x50:
            raise DumpError("RomFS image has no supported decrypted RomFS header")
        locations, spans = {}, []
        for index, name in enumerate(("directory_hash", "directory_metadata", "file_hash", "file_metadata")):
            offset, size = struct.unpack_from("<QQ", header, 8 + index * 16)
            if offset < 0x50 or offset > info.st_size or size > info.st_size - offset:
                raise DumpError(f"RomFS {name} table exceeds the image")
            if name.endswith("hash") and size % 4:
                raise DumpError(f"RomFS {name} table is not word aligned")
            if size:
                for start, end in spans:
                    if max(offset, start) < min(offset + size, end):
                        raise DumpError("RomFS metadata tables overlap")
                spans.append((offset, offset + size))
            locations[name] = {"offset": offset, "bytes": size}
        data_offset = struct.unpack_from("<Q", header, 0x48)[0]
        if data_offset < 0x50 or data_offset > info.st_size:
            raise DumpError("RomFS data offset exceeds the image")
        # Metadata may be placed after file data; only its occupied ranges are
        # validated here. No traversal or payload-integrity claim is made.
        if locations["directory_metadata"]["bytes"] < 0x18:
            raise DumpError("RomFS has no complete root directory entry")
        source.seek(locations["directory_metadata"]["offset"])
        root = source.read(0x18)
        if len(root) != 0x18 or struct.unpack_from("<I", root, 0x14)[0] != 0:
            raise DumpError("RomFS root directory entry is invalid")
    return {"format": "standard-romfs", "file_bytes": info.st_size,
            "header_sha256": hashlib.sha256(header).hexdigest(), "tables": locations,
            "data_offset": data_offset, "file_payloads_verified": False}
