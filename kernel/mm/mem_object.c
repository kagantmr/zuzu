#include "mem_object.h"
#include "kernel/mm/alloc.h"
#include <string.h>
#include "core/ensure.h"

static KHeapSlabCache mem_obj_cache;

MemObject *MemObjAlloc(void)
{
    if (!mem_obj_cache.obj_size)
        KSlabInit(&mem_obj_cache, "MemObject", sizeof(MemObject));
    return KSlabAlloc(&mem_obj_cache);
}

void MemObjFree(MemObject *mem) { KSlabFree(&mem_obj_cache, mem); }

MemObject *MemObjCreateDevice(PhysAddr phys_base, size_t size, const char *compatible, Irq irq)
{
    MemObject *mem = MemObjAlloc();
    ENSURE_RET(mem, NULL);
    mem->kind = MEMTYPE_DEVICE;
    mem->ref_count = 1;
    mem->dev.phys_base = phys_base;
    mem->dev.size = size;
    mem->dev.irq = irq;
    strncpy(mem->dev.compatible, compatible, sizeof(mem->dev.compatible) - 1);
    return mem;
}

MemObject *MemObjCreateShm(PhysAddr *page_addrs, size_t page_count)
{
    MemObject *mem = MemObjAlloc();
    ENSURE_RET(mem, NULL);
    mem->kind = MEMTYPE_SHARED;
    mem->ref_count = 1;
    mem->shm.page_addrs = page_addrs;
    mem->shm.page_count = page_count;
    return mem;
}

void MemObjDestroy(MemObject *mem)
{
    if (!mem) return;
    if (mem->ref_count > 0) mem->ref_count--;
    if (mem->ref_count == 0)
    {
        if (mem->kind == MEMTYPE_SHARED)
        {
            for (size_t i = 0; i < mem->shm.page_count; i++)
                if (mem->shm.page_addrs[i] != 0) /* demand-paged: skip unfaulted slots */
                    PmmFreeFrame(mem->shm.page_addrs[i]);
            KFree(mem->shm.page_addrs);
        }
        MemObjFree(mem);
    }
}