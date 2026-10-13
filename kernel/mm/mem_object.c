#include "mem_object.h"
#include "core/ensure.h"
#include "kernel/mm/alloc.h"
#include <arch/cache.h>
#include <string.h>

static KSlabCache mem_obj_cache;
static KSlabCache dma_map_cache;

static MemObject *MemObjAlloc(void)
{
    if (!mem_obj_cache.obj_size)
        KSlabInit(&mem_obj_cache, sizeof(MemObject));
    return KSlabAlloc(&mem_obj_cache);
}

static void MemObjFree(MemObject *mem) { KSlabFree(&mem_obj_cache, mem); }

static DmaMapping *DmaMappingAlloc(void)
{
    if (!dma_map_cache.obj_size)
        KSlabInit(&dma_map_cache, sizeof(DmaMapping));
    return KSlabAlloc(&dma_map_cache);
}

static void DmaMappingFree(DmaMapping *m) { KSlabFree(&dma_map_cache, m); }

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
    mem->dev.dma_limit = UINTPTR_MAX;
    mem->dev.bus_offset = 0;
    mem->dev.dma_maps = NULL;
    mem->dev.dma_next_id = 1;
    mem->dev.dma_map_count = 0;
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
        if (mem->kind == MEMKIND_DEVICE) {
            DmaMapping *m = mem->dev.dma_maps;
            while (m) {
                DmaMapping *next = m->next;
                MemObjUnref(m->mem);
                DmaMappingFree(m);
                m = next;
            }
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

Err MemObjDmaMap(MemObject *dev, MemObject *mem, size_t offset, size_t len, DmaDir dir,
                 uintptr_t *bus_addr, uint32_t *id)
{
    ENSURE_RET(dev->kind == MEMKIND_DEVICE, ERR_BADTYPE);
    ENSURE_RET(mem->kind == MEMKIND_SHARED && (mem->shm.flags & MEM_CONTIG), ERR_BADTYPE);
    ENSURE_RET(dir == DMA_FROM_DEVICE || dir == DMA_TO_DEVICE || dir == DMA_BIDIRECTIONAL,
               ERR_BADARG);
    ENSURE_RET(len > 0, ERR_BADARG);
    ENSURE_RET(!(offset & (PAGE_SIZE - 1)) && !(len & (PAGE_SIZE - 1)), ERR_BADARG);

    size_t size = mem->shm.page_count * PAGE_SIZE;
    ENSURE_RET(offset < size && len <= size - offset, ERR_BADARG);
    PhysAddr phys = mem->shm.page_addrs[0] + offset;
    uintptr_t bus = phys + dev->dev.bus_offset;
    ENSURE_RET(len - 1 <= dev->dev.dma_limit && bus <= dev->dev.dma_limit - (len - 1), ERR_BADARG);
    ENSURE_RET(dev->dev.dma_map_count < DMA_MAX_MAPPINGS, ERR_NOMEM);

    if (!(mem->shm.flags & MEM_UNCACHED))
        ArchCacheCleanInvalidateDcacheRange(PA_TO_VA(phys), len);

    DmaMapping *m = DmaMappingAlloc();
    ENSURE_RET(m, ERR_NOMEM);

    m->id = dev->dev.dma_next_id++;
    if (dev->dev.dma_next_id == 0)
        dev->dev.dma_next_id = 1;
    m->mem = mem;
    m->dir = dir;
    m->len = len;
    m->offset = offset;
    m->bus_addr = bus;

    MemObjRef(mem);
    m->next = dev->dev.dma_maps;
    dev->dev.dma_maps = m;
    dev->dev.dma_map_count++;

    *bus_addr = bus;
    *id = m->id;
    return ZUZU_OK;
}

Err MemObjDmaUnmap(MemObject *dev, uint32_t id)
{
    ENSURE_RET(dev->kind == MEMKIND_DEVICE, ERR_BADTYPE);

    DmaMapping **link = &dev->dev.dma_maps;
    while (*link) {
        DmaMapping *m = *link;
        if (m->id == id) {
            if ((m->dir & DMA_FROM_DEVICE) && !(m->mem->shm.flags & MEM_UNCACHED)) {
                PhysAddr phys = m->mem->shm.page_addrs[0] + m->offset;
                ArchCacheInvalidateDcacheRange(PA_TO_VA(phys), m->len);
            }
            *link = m->next;
            dev->dev.dma_map_count--;
            MemObjUnref(m->mem);
            DmaMappingFree(m);
            return ZUZU_OK;
        }
        link = &m->next;
    }
    return ERR_NOENT;
}
    
Err MemObjDmaSync(MemObject *dev, uintptr_t bus, size_t len, DmaSyncOp op)
{
    ENSURE_RET(dev->kind == MEMKIND_DEVICE, ERR_BADTYPE);
    ENSURE_RET(op <= DMA_SYNC_FOR_CPU, ERR_BADARG);
    ENSURE_RET(len > 0 && !(bus & (DMA_ALIGN - 1)) && !(len & (DMA_ALIGN - 1)), ERR_BADARG);

    for (DmaMapping *m = dev->dev.dma_maps; m; m = m->next) {
        if (len > m->len || bus < m->bus_addr || bus - m->bus_addr > m->len - len)
            continue;

        if (m->mem->shm.flags & MEM_UNCACHED)
            return ZUZU_OK;

        PhysAddr phys = m->mem->shm.page_addrs[0] + m->offset + (bus - m->bus_addr);
        if (op == DMA_SYNC_FOR_DEVICE)
            ArchCacheCleanInvalidateDcacheRange(PA_TO_VA(phys), len);
        else if (m->dir & DMA_FROM_DEVICE)
            ArchCacheInvalidateDcacheRange(PA_TO_VA(phys), len);
        return ZUZU_OK;
    }   
    return ERR_NOENT;
}

const DmaMapping *MemObjDmaNth(const MemObject *dev, size_t index)
{
    if (dev->kind != MEMKIND_DEVICE)
        return NULL;
    const DmaMapping *m = dev->dev.dma_maps;
    while (m && index--)
        m = m->next;
    return m;
}

const DmaMapping *MemObjDmaFind(const MemObject *dev, uint32_t id)
{
    if (dev->kind != MEMKIND_DEVICE)
        return NULL;
    for (const DmaMapping *m = dev->dev.dma_maps; m; m = m->next)
        if (m->id == id)
            return m;
    return NULL;
}
