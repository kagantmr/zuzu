#include "vmm.h"
#include "kernel/space/space.h"
#include "kernel/mm/pmm/pmm.h"
#include <zuzu/err.h>
#include "core/ensure.h"

Err VmmMapAnon(SpaceObject *space, VirtAddr hint, size_t size, MemProt prot, VirtAddr *out)
{
    if (size == 0)
        return ERR_BADARG;
    if (size > 32 * 1024 * 1024) // 32MB static cap, same as the old code
        return ERR_OVERFLOW;
    if (size % PAGE_SIZE != 0)
        return ERR_BADARG;

    VirtAddr va;
    if (hint != 0)
    {
        if (hint % PAGE_SIZE != 0)
            return ERR_BADARG;
        if (hint >= USER_VA_TOP || size > USER_VA_TOP - hint)
            return ERR_BADARG;
        va = hint;
    }
    else
    {
        va = VmmFindFreeVa(space->as, USER_MMAP_BASE, USER_DEVICE_BASE, size);
        if (va == 0)
            return ERR_NOMEM;
    }

    VirtMemRegion region = {
        .vaddr_start = va,
        .paddr_start = 0, // filled in lazily at fault time
        .size = size,
        .prot = prot | VM_PROT_USER,
        .memtype = VM_MEM_NORMAL,
        .owner = VM_OWNER_ANON,
        .flags = VM_FLAG_NONE,
    };
    if (!VmmAddRegion(space->as, &region))
        return ERR_NOMEM;

    *out = va;
    return ZUZU_OK;
}

Err VmmMapMemObject(SpaceObject *space, HandleTableEntry *entry, MemProt prot, VirtAddr hint, VirtAddr *out) {
    if (entry->mapped_va != 0)
        return ERR_BUSY;

    MemObject *mem = entry->mem;
    
    switch (->kind) {
        case MEMTYPE_SHARED: {
            size_t size = entry->mem->shm.page_count * PAGE_SIZE;
        
            const VirtAddr va_base = VmmFindFreeVa(space->as, USER_MMAP_BASE, USER_DEVICE_BASE, size);
            if (va_base == 0)
                return ERR_NOMEM;
        
            VirtMemRegion region = {
                .vaddr_start = va_base,
                .size = size,
                .prot = prot | VM_PROT_USER,
                .memtype = VM_MEM_NORMAL,
                .owner = VM_OWNER_SHARED,
                .backing = mem,
                .flags = VM_FLAG_NONE};
            if (!VmmAddRegion(space->as, &region))
                return ERR_NOMEM; // OOM

        } break;
        case MEMTYPE_DEVICE: {
         
        } break;
        default: {
            return ERR_BADTYPE;
        } break;
    }
    
    e->mapped_va = va_base;
    *out = va_base;
    return ZUZU_OK;
}

Err VmmUnmapUserRegion(SpaceObject *space, VirtAddr va) {
    return ZUZU_OK;
}

Err VmmProtectUserRange(SpaceObject *space, VirtAddr va, size_t size, MemProt new_prot) {
    return ZUZU_OK;
}


bool VmmMapUserPage(AddressSpace *as, PhysAddr pa, VirtAddr va, MemProt prot) {
    if (!as) return false;
    if (as->type != ADDRSPACE_USER) return false;
    if ((pa % PAGE_SIZE) != 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;

    return VmmMapRange(as, va, pa, PAGE_SIZE, prot | VM_PROT_USER,
                         VM_MEM_NORMAL, VM_OWNER_SHARED, VM_FLAG_NONE);
}

bool VmmCheckUserFault(AddressSpace *as, VirtAddr va, size_t len, bool write) {
    if (!as)
        return false;
    if (len == 0)
        return true;
    if (va > UINTPTR_MAX - len)
        return false;

    const uintptr_t end = va + len;
    if (as->type == ADDRSPACE_USER && (va >= USER_VA_TOP || end > USER_VA_TOP))
        return false;

    uintptr_t page_va = align_down(va, PAGE_SIZE);
    const uintptr_t end_va = align_up(end, PAGE_SIZE);

    while (page_va < end_va) {
        if (ArchMmuTranslate(as->pt_root_physaddr, page_va) != 0) {
            // Already mapped — nothing to do
            page_va += PAGE_SIZE;
            continue;
        }
        VirtMemRegion *r = VmmFindRegion(as, page_va);
        if (!r)
            return false;
        if (r->flags & VM_FLAG_GUARD)
            return false;
        if (!(r->prot & PROT_READ))
            return false;
        if (write && !(r->prot & PROT_WRITE))
            return false;

        if (!VmmPageFaultHandle(as, r, page_va))
            return false;

        page_va += PAGE_SIZE;
    }

    return true;
}
