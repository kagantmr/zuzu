#ifndef _ZUZU_MEM_OBJECT_H
#define _ZUZU_MEM_OBJECT_H

#include <zuzu/types.h>
#include "pmm.h"

typedef enum
{
    MEMTYPE_NONE,
    MEMTYPE_DEVICE,
    MEMTYPE_SHARED,
    MEMTYPE_COUNT
} MemType;

typedef struct {
    MemType kind;      // MEMTYPE_DEVICE / MEMTYPE_SHARED
    size_t ref_count;
    union {
        struct { PhysAddr phys_base; size_t size; char compatible[32]; Irq irq; } dev;
        struct { PhysAddr *page_addrs; size_t page_count; } shm;
    };
} MemObject;

static inline PhysAddr MemObjPageAt(MemObject *mem, size_t index) {
    return (mem->kind == MEMTYPE_DEVICE)
        ? mem->dev.phys_base + (index * PAGE_SIZE)
        : mem->shm.page_addrs[index];
}

MemObject *MemObjAlloc(void);
void MemObjFree(MemObject *mem);

MemObject *MemObjCreateShm(PhysAddr *page_addrs, size_t page_count);
MemObject *MemObjCreateDevice(PhysAddr phys_base, size_t size, const char *compatible, Irq irq);
void MemObjDestroy(MemObject *mem);

#endif /* _ZUZU_MEM_OBJECT_H */