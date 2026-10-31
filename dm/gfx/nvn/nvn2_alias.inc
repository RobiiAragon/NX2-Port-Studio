// SPDX-License-Identifier: GPL-3.0-or-later
// Initial contract: one fresh mapping per virtual reservation, one GPU view per
// physical allocation. No image aliases, duplicate views, remap or unmap.
// All registry/source mutations use g_vp_mu. Buffer deallocators only use atomics
// and munmap, so they never invert the Metal registry or residency lock order.
#if DM_GAME_NVN2
typedef struct nvn2_source {
    struct nvn2_source *next;
    pool_t *pool;
    uint64_t generation;
    uint8_t *memory;
    uint64_t size;
    uint32_t flags;
    _Atomic unsigned pins, aliases;
} nvn2_source;
static nvn2_source *g_n2_sources;
struct nvn2_alias_lease {
    _Atomic unsigned references;
    nvn2_source *source;
    uint64_t generation;
    uint8_t *base;
    uint64_t length, physical_offset, virtual_offset, size;
    uint32_t storage_class;
    const pool_t *virtual_pool;
    int owns_reservation;
};
static nvn2_source *nvn2_source_find(const pool_t *pool) {
    for (nvn2_source *s=g_n2_sources;s;s=s->next) if (s->pool==pool) return s;
    return NULL;
}
static int nvn2_pool_identity_registered(const pool_t *pool) {
    // Inspect the ledger before Initialize touches the object, independent of
    // replacement builder flags or guest bytes. Release this lock before any
    // registration/Metal call. Concurrent Init/Finalize on the SAME object is
    // outside the contract; a second sequential Init must preserve live leases.
    pthread_mutex_lock(&g_vp_mu);
    int registered=nvn2_source_find(pool)!=NULL || vpool_of(pool)!=NULL;
    pthread_mutex_unlock(&g_vp_mu); return registered;
}
static int nvn2_source_live(const nvn2_source *s) {
    return s && s->pool->magic==MAGIC('P','O','O','L') && s->pool->reserved1==s->generation &&
        s->pool->mem==s->memory && s->pool->size==s->size && s->pool->flags==s->flags && !s->pool->gpu_mem;
}
static int nvn2_source_register(pool_t *pool) {
    nvn2_source *s=calloc(1,sizeof *s); if (!s) return 0;
    s->pool=pool; s->generation=nvn_new_serial(); s->memory=pool->mem; s->size=pool->size; s->flags=pool->flags;
    pthread_mutex_lock(&g_vp_mu);
    if (nvn2_source_find(pool)) { pthread_mutex_unlock(&g_vp_mu); free(s); return 0; }
    pool->reserved1=s->generation; s->next=g_n2_sources; g_n2_sources=s;
    pthread_mutex_unlock(&g_vp_mu); return 1;
}
static enum nvn2_alias_result nvn2_source_close(pool_t *pool,int commit) {
    pthread_mutex_lock(&g_vp_mu); nvn2_source *s=nvn2_source_find(pool);
    enum nvn2_alias_result result=NVN2_ALIAS_OK;
    // Physical pools with other supported CPU policies can finalize, but only
    // the explicitly validated 0x18c policy may supply an alias in this unit.
    if (!nvn2_source_live(s)) result=NVN2_ALIAS_INVALID;
    else if (atomic_load_explicit(&s->pins,memory_order_acquire)) result=NVN2_ALIAS_PINNED;
    else if (commit) {
        nvn2_source **link=&g_n2_sources; while (*link!=s) link=&(*link)->next;
        *link=s->next; pool->reserved1=0; free(s);
    }
    pthread_mutex_unlock(&g_vp_mu); return result;
}
static void nvn2_alias_retain(nvn2_alias_lease *lease) { atomic_fetch_add_explicit(&lease->references,1,memory_order_relaxed); }
static void nvn2_alias_release(void *pointer) {
    nvn2_alias_lease *lease=pointer;
    if (!lease || atomic_fetch_sub_explicit(&lease->references,1,memory_order_acq_rel)!=1) return;
    if (lease->owns_reservation) munmap(lease->base,lease->length);
    // Source Finalize sees zero only once the last borrower and the no-copy
    // buffer have gone. Never access the source after publishing that zero.
    atomic_fetch_sub_explicit(&lease->source->aliases,1,memory_order_release);
    atomic_fetch_sub_explicit(&lease->source->pins,1,memory_order_release);
    free(lease);
}
static enum nvn2_alias_result nvn2_alias_fresh(pool_t *pool,int count,const map_req *request) {
    if (count!=1 || !request) return NVN2_ALIAS_UNSUPPORTED;
    // Caller supplies a bounded, copied request; no original GPU driver runs.
    const map_req r=*request;
    if (!r.phys || !r.size) return NVN2_ALIAS_UNSUPPORTED;
    if (r.poff<0 || r.voff<0 || ((uint64_t)r.poff|(uint64_t)r.voff|r.size)&65535) return NVN2_ALIAS_INVALID;
    if (r.storage_class!=0x4004 && r.storage_class!=0x100) return NVN2_ALIAS_UNSUPPORTED;
    pthread_mutex_lock(&g_vp_mu);
    struct vpool *v=vpool_of(pool); nvn2_source *source=nvn2_source_find((const pool_t *)r.phys);
    enum nvn2_alias_result result=NVN2_ALIAS_INVALID;
    if (!v || pool->magic!=MAGIC('P','O','O','L') || pool->flags!=0x2a1 || !nvn2_source_live(source) || source->flags!=0x18c ||
        (uint64_t)r.voff>v->len || r.size>v->len-(uint64_t)r.voff ||
        (uint64_t)r.poff>source->size || r.size>source->size-(uint64_t)r.poff ||
        ((uintptr_t)source->memory & ((uintptr_t)getpagesize()-1))) goto done;
    result=NVN2_ALIAS_UNSUPPORTED;
    if (v->n2 || v->nrun || atomic_load_explicit(&source->aliases,memory_order_acquire)) goto done;
    // Different physical objects wrapping overlapping bytes would otherwise
    // evade the one-view rule. Retired buffers keep their source in this list.
    uintptr_t first=(uintptr_t)source->memory,last=first+source->size;
    if (last<first) { result=NVN2_ALIAS_INVALID; goto done; }
    for (nvn2_source *s=g_n2_sources;s;s=s->next) if (s!=source && atomic_load_explicit(&s->aliases,memory_order_acquire) &&
        first<(uintptr_t)s->memory+s->size && (uintptr_t)s->memory<last) goto done;
    nvn2_alias_lease *lease=calloc(1,sizeof *lease);
    result=NVN2_ALIAS_NO_MEMORY; if (!lease) goto done;
    atomic_init(&lease->references,1); lease->source=source; lease->generation=source->generation;
    lease->base=v->base; lease->length=v->len; lease->physical_offset=(uint64_t)r.poff;
    lease->virtual_offset=(uint64_t)r.voff; lease->size=r.size; lease->storage_class=(uint32_t)r.storage_class;
    lease->virtual_pool=pool;
    atomic_fetch_add_explicit(&source->pins,1,memory_order_relaxed);
    atomic_fetch_add_explicit(&source->aliases,1,memory_order_relaxed);
    vm_address_t target=(uintptr_t)v->base+(uint64_t)r.voff;
    vm_prot_t current,maximum;
    kern_return_t kr=vm_remap(mach_task_self(),&target,r.size,0,VM_FLAGS_FIXED|VM_FLAGS_OVERWRITE,
        mach_task_self(),(uintptr_t)source->memory+(uint64_t)r.poff,FALSE,&current,&maximum,VM_INHERIT_NONE);
    result=NVN2_ALIAS_OS_ERROR;
    if (kr!=KERN_SUCCESS) { nvn2_alias_release(lease); goto done; }
    int mapped=target==(uintptr_t)v->base+(uint64_t)r.voff;
    if (mapped && !mprotect((void *)target,r.size,PROT_READ|PROT_WRITE)) {
        nvn2_alias_retain(lease); // consumed even if Metal refuses the allocation
        mapped=mtl_pool_add_owned((void *)target,r.size,lease,nvn2_alias_release);
        if (mapped) {
            // Publish only after both the OS alias and no-copy buffer exist.
            lease->owns_reservation=1; v->n2=lease; v->nrun=1;
            v->run[0].lo=(uint64_t)r.voff; v->run[0].hi=(uint64_t)r.voff+r.size;
            v->top=v->run[0].hi; atomic_fetch_add(&g_vp_gen,1);
            result=NVN2_ALIAS_OK; goto done;
        }
    }
    // A failed construction leaves the original inaccessible reservation and
    // registry intact. If restoring the VM contract fails, continuing is unsafe.
    if (mmap(v->base+(uint64_t)r.voff,r.size,PROT_NONE,MAP_ANON|MAP_PRIVATE|MAP_FIXED,-1,0)==MAP_FAILED)
        die("[RED] NVN2 failed alias could not restore its inaccessible reservation");
    nvn2_alias_release(lease);
done:
    pthread_mutex_unlock(&g_vp_mu); return result;
}
static enum nvn2_alias_result nvn2_alias_acquire(uint64_t address,uint64_t size,nvn2_alias_lease **out,const uint8_t **physical) {
    if (!size || size>UINT64_MAX-address) return NVN2_ALIAS_INVALID;
    enum nvn2_alias_result result=NVN2_ALIAS_INVALID;
    pthread_mutex_lock(&g_vp_mu);
    for (int i=0;i<NVPOOL;++i) {
        struct vpool *v=&g_vp[i]; nvn2_alias_lease *lease=v->n2;
        if (!lease || address<(uintptr_t)v->base || address-(uintptr_t)v->base<lease->virtual_offset) continue;
        uint64_t offset=address-(uintptr_t)v->base-lease->virtual_offset;
        if (offset>lease->size || size>lease->size-offset || !nvn2_source_live(lease->source) || lease->generation!=lease->source->generation) continue;
        nvn2_alias_retain(lease); *out=lease;
        *physical=lease->source->memory+lease->physical_offset+offset; result=NVN2_ALIAS_OK; break;
    }
    pthread_mutex_unlock(&g_vp_mu); return result;
}
static void nvn2_virtual_close(const pool_t *pool) {
    pthread_mutex_lock(&g_vp_mu); struct vpool *v=vpool_of(pool);
    if (!v) { pthread_mutex_unlock(&g_vp_mu); return; }
    nvn2_alias_lease *lease=v->n2; uint8_t *base=v->base; uint64_t length=v->len;
    for (uint64_t i=0;i<(v->size+(2u<<20)-1)>>21;++i) free(v->pt[i]);
    free(v->pt); memset(v,0,sizeof *v); // closed: no new CPU leases
    pthread_mutex_unlock(&g_vp_mu);
    if (lease) {
        mtl_pool_del_owned(base+lease->virtual_offset);
        nvn2_alias_release(lease); // VA remains owned by any buffer/CPU borrowers
    } else munmap(base,length);
}
static int nvn2_alias_flush_lease(nvn2_alias_lease *lease,uint64_t offset,uint64_t size) {
    if (!lease || offset>lease->size || size>lease->size-offset ||
        lease->generation!=lease->source->generation) return 0;
    // This remains valid after the public virtual pool closes. The lease pins
    // its source, and publication uses no retired or reusable virtual address.
    fl_add(lease->source->memory+lease->physical_offset+offset,size); return 1;
}
static int nvn2_pool_flush(const pool_t *pool,uint64_t offset,uint64_t size) {
    if (!size) return 1;
    nvn2_alias_lease *lease=NULL; const uint8_t *physical=NULL;
    if (pool->flags&0x200) {
        if (nvn2_alias_acquire((uintptr_t)pool->gpu_mem+offset,size,&lease,&physical)!=NVN2_ALIAS_OK) return 0;
    } else {
        pthread_mutex_lock(&g_vp_mu); nvn2_source *source=nvn2_source_find(pool);
        if (!nvn2_source_live(source) || offset>source->size || size>source->size-offset) { pthread_mutex_unlock(&g_vp_mu); return 0; }
        atomic_fetch_add_explicit(&source->pins,1,memory_order_relaxed); physical=source->memory+offset;
        pthread_mutex_unlock(&g_vp_mu);
        fl_add(physical,size); // canonical only: no delayed publication to retired VA
        atomic_fetch_sub_explicit(&source->pins,1,memory_order_release); return 1;
    }
    int result=nvn2_alias_flush_lease(lease,(uint64_t)(physical-lease->source->memory)-lease->physical_offset,size);
    nvn2_alias_release(lease); return result;
}
static int nvn2_alias_flushed(uint64_t address,uint64_t size,int *handled) {
    *handled=0;
    pthread_mutex_lock(&g_vp_mu);
    for (int i=0;i<NVPOOL;++i) if (g_vp[i].base && address>=(uintptr_t)g_vp[i].base && address-(uintptr_t)g_vp[i].base<g_vp[i].len) { *handled=1; break; }
    pthread_mutex_unlock(&g_vp_mu);
    if (!*handled) return 0;
    nvn2_alias_lease *lease=NULL; const uint8_t *physical=NULL;
    if (nvn2_alias_acquire(address,size,&lease,&physical)!=NVN2_ALIAS_OK) return 0;
    pthread_mutex_lock(&g_fl_mu);
    size_t i=fl_find(physical,0); int result=i<g_nfl && g_fl[i].lo<physical+size;
    pthread_mutex_unlock(&g_fl_mu); nvn2_alias_release(lease); return result;
}
static int nvn2_alias_since(uint64_t address,uint64_t size,uint64_t stamp) {
    nvn2_alias_lease *lease=NULL; const uint8_t *physical=NULL;
    if (nvn2_alias_acquire(address,size,&lease,&physical)!=NVN2_ALIAS_OK) return 1;
    // CPU writes are tracked at the physical address. GPU/host alias writes do
    // not fault that mapping, so a live writable GPU view is conservatively
    // dirty even if the physical generation is clean. Images remain gated.
    int result=nvn_dirty_since(physical,size,stamp) || atomic_load_explicit(&lease->source->aliases,memory_order_acquire)!=0;
    nvn2_alias_release(lease); return result;
}
#endif
