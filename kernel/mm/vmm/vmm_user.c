#include "core/ensure.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/space/space.h"
#include "vmm.h"
#include "vmm_internal.h"
#include <arch/barrier.h>
#include <arch/cache.h>
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

Err InjectInKittenSpace(SpaceObject *kitten, SpaceObject *parent, InjectArgs *args) {

    ENSURE_RET(args->len, ERR_BADARG);
    ENSURE_RET(!(args->prot & ~(uint32_t)(PROT_EXEC|PROT_WRITE|PROT_READ)), ERR_BADARG);
    ENSURE_RET(!((args->prot & PROT_WRITE) && (args->prot & PROT_EXEC)), ERR_BADARG);
    ENSURE_RET(args->dest_vaddr < USER_VA_TOP && args->len <= USER_VA_TOP - args->dest_vaddr, ERR_BADARG);
    ENSURE_RET(args->dest_vaddr % PAGE_SIZE == 0, ERR_BADARG);
    
    ENSURE_RET(list_empty(&kitten->tasks), ERR_BUSY);
    
    if (args->flags & ASINJECT_FLAG_RESERVE)
    {
        /* Reserve-only mode: register anon memory in the target AS with no
         * pages allocated or copied; pages get allocated, zeroed, and mapped
         * lazily on first touch via the normal fault path. */
        ENSURE_RET(args->src_buf == NULL && args->len % PAGE_SIZE == 0, ERR_BADARG);

        VirtMemRegion region = {
            .vaddr_start = args->dest_vaddr,
            .size = args->len,
            .prot = args->prot | VM_PROT_USER,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_ANON,
            .flags = VM_FLAG_NONE,
        };
        ENSURE_RET(VmmAddRegion(kitten->as, &region), ERR_NOMEM);

        return ZUZU_OK;
    }

    ENSURE_RET(args->src_buf != NULL, ERR_BADARG);

    size_t page_count = (args->len + PAGE_SIZE - 1) / PAGE_SIZE;

    /* If the destination lies entirely inside an existing anon region (e.g.
     * a pre-reserved stack), fill pages in place instead of creating a new
     * region. Injected prot must not exceed the region's own prot. */
    VirtMemRegion *enclosing = NULL;
    for (uint32_t i = 0; i < kitten->as->regions.len; i++)
    {
        VirtMemRegion *r = vm_region_vec_get(&kitten->as->regions, i);
        if (!r)
            continue;
        if (args->dest_vaddr >= r->vaddr_start &&
            args->dest_vaddr - r->vaddr_start < r->size &&
            page_count * PAGE_SIZE <= r->size - (args->dest_vaddr - r->vaddr_start))
        {
            enclosing = r;
            break;
        }
    }
    if (enclosing)
    {
        ENSURE_RET(!(enclosing->flags & VM_FLAG_GUARD) &&
                       enclosing->owner == VM_OWNER_ANON &&
                       enclosing->memtype == VM_MEM_NORMAL &&
                       !((args->prot | VM_PROT_USER) & ~enclosing->prot),
                   ERR_BADARG);
    }

    PhysAddr *page_addrs = KCalloc(page_count, sizeof(PhysAddr));
    ENSURE_RET(page_addrs, ERR_NOMEM);

    for (size_t i = 0; i < page_count; i++)
    {
        VirtAddr dst_page = args->dest_vaddr + (i * PAGE_SIZE);

        PhysAddr page = enclosing ? ArchMmuTranslate(kitten->as->pt_root_physaddr, dst_page) : 0;
        bool fresh = (page == 0);
        if (fresh)
        {
            page = PmmAllocFrame();
            if (!page)
                goto rollback_nomem;
            page_addrs[i] = page;
        }

        size_t offset = (i * PAGE_SIZE);
        size_t bytes_to_copy = args->len - offset;
        if (bytes_to_copy > PAGE_SIZE)
            bytes_to_copy = PAGE_SIZE;

        if (!VmmCheckUserFault(parent->as, (uintptr_t)args->src_buf + offset, bytes_to_copy, false))
            goto rollback_badarg;
        memcpy((void *)PA_TO_VA(page), (const void *)((uintptr_t)args->src_buf + offset), bytes_to_copy);

        if (fresh && bytes_to_copy < PAGE_SIZE)
            memset((void *)(PA_TO_VA(page) + bytes_to_copy), 0, PAGE_SIZE - bytes_to_copy);

        if (fresh && !VmmMapUserPage(kitten->as, page, dst_page, args->prot))
        {
            PmmFreeFrame(page);
            page_addrs[i] = 0;
            goto rollback_nomem;
        }
    }

    if (args->prot & PROT_EXEC)
    {
        for (size_t i = 0; i < page_count; i++)
        {
            PhysAddr pa = ArchMmuTranslate(kitten->as->pt_root_physaddr,
                                            args->dest_vaddr + (i * PAGE_SIZE));
            if (pa)
                arch_cache_clean_dcache_range(PA_TO_VA(pa), PAGE_SIZE);
        }
        arch_cache_invalidate_icache_all();
    }

    if (!enclosing)
    {
        VirtMemRegion region = {
            .vaddr_start = args->dest_vaddr,
            .size = page_count * PAGE_SIZE,
            .prot = args->prot | VM_PROT_USER,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_ANON,
            .flags = VM_FLAG_NONE,
        };
        if (!VmmAddRegion(kitten->as, &region))
            goto rollback_nomem;
    }

    KFree(page_addrs);
    return ZUZU_OK;

rollback_badarg:
    for (size_t j = 0; j < page_count; j++)
    {
        if (page_addrs[j])
        {
            VmmUnmapRange(kitten->as, args->dest_vaddr + (j * PAGE_SIZE), PAGE_SIZE, true);
            PmmFreeFrame(page_addrs[j]);
        }
    }
    KFree(page_addrs);
    return ERR_BADARG;

rollback_nomem:
    for (size_t j = 0; j < page_count; j++)
    {
        if (page_addrs[j])
        {
            VmmUnmapRange(kitten->as, args->dest_vaddr + (j * PAGE_SIZE), PAGE_SIZE, true);
            PmmFreeFrame(page_addrs[j]);
        }
    }
    KFree(page_addrs);
    return ERR_NOMEM;
}