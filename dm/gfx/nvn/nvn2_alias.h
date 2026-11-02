// SPDX-License-Identifier: GPL-3.0-or-later
// Internal NVN2 fresh buffer-alias contract. Include after nvn_obj.h.
#pragma once
#if DM_GAME_NVN2
enum nvn2_alias_result {
    NVN2_ALIAS_OK, NVN2_ALIAS_INVALID, NVN2_ALIAS_UNSUPPORTED,
    NVN2_ALIAS_PINNED, NVN2_ALIAS_OS_ERROR, NVN2_ALIAS_NO_MEMORY
};
typedef struct nvn2_alias_lease nvn2_alias_lease;
static inline const char *nvn2_alias_error(enum nvn2_alias_result result) {
    switch (result) {
        case NVN2_ALIAS_OK: return "success";
        case NVN2_ALIAS_INVALID: return "invalid range, flags or pool generation";
        case NVN2_ALIAS_UNSUPPORTED: return "unsupported map count, class, remap, unmap or duplicate view";
        case NVN2_ALIAS_PINNED: return "physical source is pinned by a live lease";
        case NVN2_ALIAS_OS_ERROR: return "OS alias or no-copy Metal allocation failed";
        case NVN2_ALIAS_NO_MEMORY: return "host lease allocation failed";
    }
    return "unknown alias error";
}
static int nvn2_source_register(pool_t *pool);
static int nvn2_pool_identity_registered(const pool_t *pool);
static enum nvn2_alias_result nvn2_source_close(pool_t *pool, int commit);
static enum nvn2_alias_result nvn2_alias_acquire(uint64_t address, uint64_t size,
                                               nvn2_alias_lease **lease, const uint8_t **physical);
static void nvn2_alias_release(void *lease);
static void nvn2_virtual_close(const pool_t *pool);
static int nvn2_pool_flush(const pool_t *pool, uint64_t offset, uint64_t size);
static int nvn2_alias_flushed(uint64_t address, uint64_t size, int *handled);
static int nvn2_alias_since(uint64_t address, uint64_t size, uint64_t stamp);
#endif
