#ifndef KERNEL_MM_MEM_OBJECT_H
#define KERNEL_MM_MEM_OBJECT_H

#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include <types.h>

typedef enum {
    MEMKIND_NONE,
    MEMKIND_DEVICE,
    MEMKIND_SHARED,
} MemKind;

#define DMA_MAX_MAPPINGS 256

struct MemObjectStruct;

typedef struct DmaMappingStruct {
    struct DmaMappingStruct *next;
    uint32_t id;
    struct MemObjectStruct *mem;
    size_t offset;
    size_t len;
    DmaDir dir;
    uintptr_t bus_addr;
} DmaMapping;

typedef struct MemObjectStruct {
    MemKind kind; // MEMKIND_DEVICE / MEMKIND_SHARED
    size_t ref_count;
    bool exec_synced;
    union {
        struct {
            PhysAddr phys_base;
            size_t size;
            Irq irq;
            uintptr_t dma_limit;
            uintptr_t bus_offset;
            DmaMapping *dma_maps;
            uint32_t dma_next_id;
            size_t dma_map_count;
        } dev;
        struct {
            PhysAddr *page_addrs;
            size_t page_count;
            CreateMemoryFlags flags;
        } shm;
    };
} MemObject;

MemObject *MemObjCreateShm(PhysAddr *page_addrs, size_t page_count, CreateMemoryFlags flags);
MemObject *MemObjCreateDevice(PhysAddr phys_base, size_t size, Irq irq);
void MemObjRef(MemObject *mem);
void MemObjUnref(MemObject *mem);
void MemObjUnmapAndDrop(AddressSpace *as, VirtAddr mapped_va, MemObject *mem);
Err MemObjDmaMap(MemObject *dev, MemObject *mem, size_t offset, size_t len, DmaDir dir,
                 uintptr_t *bus_addr, uint32_t *id);
Err MemObjDmaUnmap(MemObject *dev, uint32_t id);
const DmaMapping *MemObjDmaNth(const MemObject *dev, size_t index);
const DmaMapping *MemObjDmaFind(const MemObject *dev, uint32_t id);
Err MemObjDmaSync(MemObject *dev, uintptr_t bus, size_t len, DmaSyncOp op);

#endif /* KERNEL_MM_MEM_OBJECT_H */