#ifndef _ZUZU_MEM_OBJECT_H
#define _ZUZU_MEM_OBJECT_H

#include <types.h>
#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"

typedef enum
{
    MEMKIND_NONE,
    MEMKIND_DEVICE,
    MEMKIND_SHARED,
} MemKind;

typedef struct {
    MemKind kind;      // MEMKIND_DEVICE / MEMKIND_SHARED
    size_t ref_count;
    union {
        struct { PhysAddr phys_base; size_t size; Irq irq; } dev;
        struct { PhysAddr *page_addrs; size_t page_count; } shm;
    };
} MemObject;

MemObject *MemObjCreateShm(PhysAddr *page_addrs, size_t page_count);
MemObject *MemObjCreateDevice(PhysAddr phys_base, size_t size, Irq irq);
void MemObjUnref(MemObject *mem);
void MemObjUnmapAndDrop(AddressSpace *as, VirtAddr mapped_va, MemObject *mem);

#endif /* _ZUZU_MEM_OBJECT_H */