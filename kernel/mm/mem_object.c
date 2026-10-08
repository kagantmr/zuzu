#include "mem_object.h"
#include "core/ensure.h"
#include "kernel/mm/alloc.h"
#include <string.h>

static KSlabCache mem_obj_cache;

static MemObject *MemObjAlloc(void)
{
    if (!mem_obj_cache.obj_size)
        KSlabInit(&mem_obj_cache, sizeof(MemObject));
    return KSlabAlloc(&mem_obj_cache);
}

static void MemObjFree(MemObject *mem) { KSlabFree(&mem_obj_cache, mem); }

MemObject *MemObjCreateDevice(PhysAddr phys_base, size_t size, Irq irq)
{
    MemObject *mem = MemObjAlloc();
    ENSURE_RET(mem, NULL);
    mem->kind = MEMKIND_DEVICE;
    mem->ref_count = 1;
    mem->exec_synced = false;
    mem->dev.phys_base = phys_base;
    mem->dev.size = size;
    mem->dev.irq = irq;
    return mem;
}

MemObject *MemObjCreateShm(PhysAddr *page_addrs, size_t page_count, CreateMemoryFlags flags)
{
    MemObject *mem = MemObjAlloc();
    ENSURE_RET(mem, NULL);
    mem->kind = MEMKIND_SHARED;
    mem->ref_count = 1;
    mem->exec_synced = false;
    mem->shm.page_addrs = page_addrs;
    mem->shm.page_count = page_count;
    mem->shm.flags = flags;
    return mem;
}

void MemObjRef(MemObject *mem)
{
    if (mem)
        mem->ref_count++;
}

void MemObjUnref(MemObject *mem)
{
    if (!mem)
        return;
    if (mem->ref_count > 0)
        mem->ref_count--;
    if (mem->ref_count == 0) {
        if (mem->kind == MEMKIND_SHARED) {
            for (size_t i = 0; i < mem->shm.page_count; i++)
                if (mem->shm.page_addrs[i] != 0) /* demand-paged: skip unfaulted slots */
                    PmmFreeFrame(mem->shm.page_addrs[i]);
            KFree(mem->shm.page_addrs);
        }
        MemObjFree(mem);
    }
}

void MemObjUnmapAndDrop(AddressSpace *as, VirtAddr mapped_va, MemObject *mem)
{
    if (mapped_va)
        VmmRemoveRegion(as, mapped_va,
                        (mem->kind == MEMKIND_DEVICE) ? mem->dev.size
                                                      : mem->shm.page_count * PAGE_SIZE);
    MemObjUnref(mem);
}