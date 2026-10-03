#ifndef _ZUZU_MEM_OBJECT_H
#define _ZUZU_MEM_OBJECT_H

#include <types.h>
#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"

typedef enum
{
    MEMTYPE_NONE,
    MEMTYPE_DEVICE,
    MEMTYPE_SHARED,
} MemType;

typedef struct {
    MemType kind;      // MEMTYPE_DEVICE / MEMTYPE_SHARED
    size_t ref_count;
    union {
        struct { PhysAddr phys_base; size_t size; Irq irq; } dev;
        struct { PhysAddr *page_addrs; size_t page_count; } shm;
    };
} MemObject;

MemObject *MemObjCreateShm(PhysAddr *page_addrs, size_t page_count);
MemObject *MemObjCreateDevice(PhysAddr phys_base, size_t size, Irq irq);
void MemObjDestroy(MemObject *mem);
void MemObjUnmapAndDrop(AddressSpace *as, VirtAddr mapped_va, MemObject *mem);

#endif /* _ZUZU_MEM_OBJECT_H */