#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t handle; unsigned object; uint64_t aruid; } dm_hid_reply;
uint32_t dm_hid_service_request(unsigned object, uint64_t aruid, uint32_t command,
                               const void *arguments, size_t size,
                               const uint32_t *copies, unsigned copy_count,
                               dm_hid_reply *reply);
// Publish the same controller state as the SDK takeovers, using the audited
// SharedMemoryFormat Npad region. The service owns the destination lifetime.
void dm_hid_write_shared(void *memory, uint64_t sequence);
