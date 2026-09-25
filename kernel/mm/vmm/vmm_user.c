#include "core/ensure.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/space/space.h"
#include "vmm.h"
#include "vmm_internal.h"
#include <arch/barrier.h>
#include <arch/mmu.h>
#include <zuzu/err.h>
#include "core/panic.h"

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

Err VmmMapMemObject(SpaceObject *space, HandleTableEntry *entry, MemProt prot, VirtAddr hint,
                    VirtAddr *out)
{
    MemObject *mem = entry->mem;

    ENSURE_RET(!entry->mapped_va, ERR_BUSY);
    ENSURE_RET(mem, ERR_BADHANDLE);

    VirtAddr va_base = 0;

    switch (mem->kind)
    {
    case MEMTYPE_SHARED:
    {
        size_t size = entry->mem->shm.page_count * PAGE_SIZE;

        if (hint == 0)
        {
            va_base = VmmFindFreeVa(space->as, USER_MMAP_BASE, USER_DEVICE_BASE, size);
        }
        else
        {
            ENSURE_RET(!(hint % PAGE_SIZE), ERR_BADARG);
            ENSURE_RET((hint >= USER_MMAP_BASE && hint < USER_DEVICE_BASE &&
                        size <= USER_DEVICE_BASE - hint),
                       ERR_BADARG);
            va_base = hint;
        }
        if (va_base == 0)
            return ERR_NOMEM;

        VirtMemRegion region = {.vaddr_start = va_base,
                                .size = size,
                                .prot = prot | VM_PROT_USER,
                                .memtype = VM_MEM_NORMAL,
                                .owner = VM_OWNER_SHARED,
                                .backing = mem,
                                .flags = VM_FLAG_NONE};
        if (!VmmAddRegion(space->as, &region))
            return ERR_NOMEM; // OOM
    }
    break;
    case MEMTYPE_DEVICE:
    {
        size_t size_aligned = align_up(mem->dev.size, PAGE_SIZE);

        if (hint == 0)
        {
            va_base = VmmFindFreeVa(space->as, USER_DEVICE_BASE, USER_DEVICE_LIMIT, size_aligned);
        }
        else
        {
            ENSURE_RET(!(hint & 0xFFF), ERR_BADARG);
            ENSURE_RET((hint >= USER_DEVICE_BASE && hint < USER_DEVICE_LIMIT &&
                        size_aligned <= USER_DEVICE_LIMIT - hint),
                       ERR_BADARG);
            va_base = hint;
        }
        if (va_base == 0)
            return ERR_NOMEM;

        if (!VmmMapRange(space->as, va_base, mem->dev.phys_base, size_aligned, prot | VM_PROT_USER,
                         VM_MEM_DEVICE, VM_OWNER_NONE, VM_FLAG_NONE))
            return ERR_NOMEM;

        VirtMemRegion region = {
            .vaddr_start = va_base,
            .paddr_start = mem->dev.phys_base,
            .backing = mem,
            .size = size_aligned,
            .prot = prot | VM_PROT_USER,
            .memtype = VM_MEM_DEVICE,
            .owner = VM_OWNER_NONE,
            .flags = VM_FLAG_NONE,
        };
        if (!VmmAddRegion(space->as, &region))
        {
            VmmUnmapRange(space->as, va_base, size_aligned, true);
            return ERR_NOMEM;
        }

        // flush TLB for this VA
        arch_mmu_flush_tlb_va(va_base);
        ArchCtxSync();
    }
    break;
    default:
    {
        return ERR_BADTYPE;
    }
    break;
    }

    entry->mapped_va = va_base;
    *out = va_base;
    return ZUZU_OK;
}

Err VmmUnmapUserRegion(SpaceObject *space, VirtAddr va) { 
    VirtMemRegion *found = VmmFindRegion(space->as, va);

    ENSURE_RET(found, ERR_NOENT);
    ENSURE_RET(found->vaddr_start == va, ERR_BADARG);
    ENSURE_RET(!(found->flags & VM_FLAG_PINNED), ERR_NOPERM);

    switch (found->owner) {
        case VM_OWNER_ANON: {
            for (VirtAddr anon_va = va; (anon_va < va + found->size); anon_va += PAGE_SIZE) {
                PhysAddr anon_pa = ArchMmuTranslate(space->as->pt_root_physaddr, anon_va);
                (anon_pa == 0) ? (void)anon_pa : PmmFreeFrame(anon_pa);
            }
        } break;
        case VM_OWNER_NONE: 
        case VM_OWNER_SHARED: {
            bool found_in_table = false;
            for (Handle i = 0; i < (Handle)HANDLE_MAX_SLOTS; i++) {
                HandleTableEntry *entry = HandleTableGet(&space->handle_table, i);
                if (!entry) continue;
                if (entry->type == HANDLE_MEM && entry->mapped_va == va) {
                    found_in_table = true;
                    entry->mapped_va = 0;
                    break;
                }
            }
            ENSURE(found_in_table, KWARN("Couldn't find unmap entry in handle table"));
        }
    }

    ENSURE(VmmRemoveRegion(space->as, found->vaddr_start, found->size), panic("Found region, but VmmRemoveRegion failed"));
    return ZUZU_OK; 
}

Err VmmProtectUserRange(SpaceObject *space, VirtAddr va, size_t size, MemProt new_prot)
{
    ENSURE_RET(0 != size, ERR_BADARG); 
    ENSURE_RET(!(size % PAGE_SIZE), ERR_BADARG); 
    ENSURE_RET(!(va % PAGE_SIZE), ERR_BADARG);
    ENSURE_RET((va < USER_VA_TOP && size <= USER_VA_TOP - va), ERR_BADARG);
    ENSURE_RET(!(new_prot & ~(uint32_t)(PROT_EXEC|PROT_WRITE|PROT_READ)), ERR_BADARG);
    ENSURE_RET(!((new_prot & PROT_WRITE) && (new_prot & PROT_EXEC)), ERR_BADARG);

    ENSURE_RET(VmmProtectPage(space->as, va, size, new_prot | VM_PROT_USER), ERR_BADARG);
    
    return ZUZU_OK;
}

bool VmmMapUserPage(AddressSpace *as, PhysAddr pa, VirtAddr va, MemProt prot)
{
    if (!as)
        return false;
    if (as->type != ADDRSPACE_USER)
        return false;
    if ((pa % PAGE_SIZE) != 0)
        return false;
    if ((va % PAGE_SIZE) != 0)
        return false;

    return VmmMapRange(as, va, pa, PAGE_SIZE, prot | VM_PROT_USER, VM_MEM_NORMAL, VM_OWNER_SHARED,
                       VM_FLAG_NONE);
}

bool VmmCheckUserFault(AddressSpace *as, VirtAddr va, size_t len, bool write)
{
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

    while (page_va < end_va)
    {
        if (ArchMmuTranslate(as->pt_root_physaddr, page_va) != 0)
        {
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
