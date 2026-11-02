// SPDX-License-Identifier: GPL-3.0-or-later
// NVN2 scratch reservations are command state. Metal compiler-managed Private
// arrays supply per-invocation local memory; these pool addresses never enter a shader.
#pragma once
#include <stdint.h>

typedef struct { const void *pool; uint64_t offset, size; } nvn2_scratch_reservation;
enum nvn2_scratch_result { NVN2_SCRATCH_VALID, NVN2_SCRATCH_RANGE, NVN2_SCRATCH_ALIGNMENT };
static inline enum nvn2_scratch_result nvn2_scratch_validate(
    const void *pool, uint64_t pool_address, uint64_t pool_size,
    int64_t offset, uint64_t size, nvn2_scratch_reservation *out) {
    nvn2_scratch_reservation value = {pool,(uint64_t)offset,size};
    if (!pool) {
        if (offset || size) return NVN2_SCRATCH_RANGE;
    } else if (!pool_size) {
        // A non-null, initialized empty pool has no address and may only clear the reservation.
        if (pool_address || offset || size) return NVN2_SCRATCH_RANGE;
    } else {
        if (!pool_address || offset<0 || (uint64_t)offset>pool_size || size>pool_size-(uint64_t)offset ||
            (uint64_t)offset>UINT64_MAX-pool_address) return NVN2_SCRATCH_RANGE;
        // Audited public constraints for an optional nonempty reservation.
        if (((pool_address+(uint64_t)offset)&4095) || (size&16383)) return NVN2_SCRATCH_ALIGNMENT;
    }
    if (out) *out=value;
    return NVN2_SCRATCH_VALID;
}
