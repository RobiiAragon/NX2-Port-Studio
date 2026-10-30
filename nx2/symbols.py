# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded ELF64 dynamic symbols from the NSO's declared rodata tables."""
import struct

from .errors import DumpError

MAX_SYMBOLS = 100_000
MAX_STRING_TABLE = 32 * 1024 * 1024


def read_symbols(symbols, strings):
    if len(symbols) % 24 or len(symbols) // 24 > MAX_SYMBOLS or len(strings) > MAX_STRING_TABLE:
        raise DumpError("NSO dynamic symbol tables exceed the supported bounds", code="invalid-symbols")
    imports, exports, weak = set(), set(), set()
    for offset in range(24, len(symbols), 24):
        name, info, _, section, _, _ = struct.unpack_from("<IBBHQQ", symbols, offset)
        if not name:
            continue
        end = strings.find(b"\0", name, min(name + 4097, len(strings)))
        if name >= len(strings) or end < 0:
            raise DumpError("NSO symbol name exceeds or is unterminated in its string table", code="invalid-symbols")
        try:
            text = strings[name:end].decode("utf-8")
        except UnicodeDecodeError as error:
            raise DumpError("NSO symbol name is not UTF-8", code="invalid-symbols") from error
        if any(ord(character) < 0x20 for character in text):
            raise DumpError("NSO symbol name contains control characters", code="invalid-symbols")
        binding = info >> 4
        if section == 0 and binding in (1, 2):
            imports.add(text)
            if binding == 2:
                weak.add(text)
        elif section != 0 and binding in (1, 2):
            exports.add(text)
    return {"status": "read", "imports": sorted(imports), "exports": sorted(exports),
            "weak_imports": sorted(weak), "import_count": len(imports), "export_count": len(exports),
            "graphics_imports": sorted(name for name in imports if name.lower().startswith(("nvn", "vk", "egl")))}
