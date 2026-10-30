// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdint.h>
#include <stddef.h>
#include "zstd.h"

// The NSO owns the output-size limit. Never allocate from an untrusted frame's
// declared size; decode directly into the caller's bounded buffer instead.
long long nx2_zbic_decode(const void *source, size_t source_size, void *output, size_t capacity) {
    if (!source || !output || !source_size || !capacity || capacity > 512u * 1024u * 1024u)
        return -1;
    const unsigned char *bytes = source;
    if (source_size < 4 || bytes[0] != 'Z' || bytes[1] != 'B' || bytes[2] != 'I' || bytes[3] != 'C')
        return -1;
    size_t frame_size = ZSTD_findFrameCompressedSize(source, source_size);
    if (ZSTD_isError(frame_size) || frame_size != source_size) return -1;
    size_t result = ZSTD_decompress(output, capacity, source, source_size);
    return ZSTD_isError(result) ? -1 : (long long)result;
}

// Fixture support: creates original synthetic ZBIC data, never game-derived bytes.
long long nx2_zbic_encode(const void *source, size_t source_size, void *output, size_t capacity) {
    size_t result = ZSTD_compress(output, capacity, source, source_size, 3);
    return ZSTD_isError(result) ? -1 : (long long)result;
}
