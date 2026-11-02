// Owned kernel memory objects shared by the SVC and service layers. A service
// retains an object, never a borrowed handle number that can be closed/reused.
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct dm_memory_object dm_memory_object;
dm_memory_object *dm_memory_retain_transfer(uint32_t handle, size_t size, unsigned permission);
void dm_memory_release(dm_memory_object *object);
void *dm_memory_data(dm_memory_object *object);
size_t dm_memory_size(dm_memory_object *object);
dm_memory_object *dm_memory_create_shared(size_t size);
uint32_t dm_memory_copy_handle(dm_memory_object *object);
