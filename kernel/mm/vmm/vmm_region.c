/* Address-space and region-list bookkeeping: create/destroy an
 * AddressSpace, add/remove/find VirtMemRegion entries, and the lazy
 * page-fault handler that realizes a region's backing on first touch. */

#include "vmm_internal.h"

#include "kernel/mm/alloc.h"
#include "kernel/mm/mem_object.h"
#include "kernel/mm/pmm/pmm.h"

#include <arch/asid.h>
#include <arch/mmu.h>
#include <stdlib.h>
#include <string.h>

#include "core/panic.h"

AddressSpace *g_kernel_as = NULL;
AddressSpace *g_current_addrspace = NULL;
bool g_mmu_enabled = false;

static KHeapSlabCache addrspace_cache;

#define LOG_FMT(fmt) "(vmm) " fmt
#include <zuzu/log.h>

static int RegionContainsVa(const void *key, const void *elem)
{
    uintptr_t va = *(const uintptr_t *)key;
    const VirtMemRegion *r = (const VirtMemRegion *)elem;
    if (va < r->vaddr_start)       return -1;
    if (va - r->vaddr_start >= r->size) return  1;
    return 0;
}

static int RegionCmpStart(const void *key, const void *elem)
{
    uintptr_t va = *(const uintptr_t *)key;
    const VirtMemRegion *r = (const VirtMemRegion *)elem;
    if (va < r->vaddr_start) return -1;
    if (va > r->vaddr_start) return  1;
    return 0;
}

AddressSpace *VmmGetKernelAddrspace(void)
{
    return g_kernel_as;
}

VirtMemRegion *VmmFindRegion(AddressSpace *as, uintptr_t va)
{
    if (!as || as->regions.len == 0)
        return NULL;
    return bsearch(&va, as->regions.data, as->regions.len,
                   sizeof(VirtMemRegion), RegionContainsVa);
}

bool VmmPageFaultHandle(AddressSpace *restrict as, VirtMemRegion *restrict r, VirtAddr page_va)
{
    if (!as || !r)
        return false;

    if (ArchMmuTranslate(as->pt_root_physaddr, page_va) != 0)
        return true;

    if (r->memtype == VM_MEM_DEVICE)
        return false;

    PhysAddr new_pa = 0;
    bool allocated_new = false;

    if (r->owner == VM_OWNER_SHARED && r->backing) {
        MemObject *mem = (MemObject *)r->backing;
        if (page_va < r->vaddr_start)
            return false;

        size_t page_index = (size_t)((page_va - r->vaddr_start) / PAGE_SIZE);
        if (page_index >= mem->shm.page_count)
            return false;

        new_pa = mem->shm.page_addrs[page_index];
        if (new_pa == 0) {
            new_pa = PmmAllocFrame();
            if (new_pa == 0)
                return false;
            memset((void *)PA_TO_VA(new_pa), 0, PAGE_SIZE);
            mem->shm.page_addrs[page_index] = new_pa;
            allocated_new = true;
        }
    } else if (r->owner == VM_OWNER_ANON) {
        new_pa = PmmAllocFrame();
        if (new_pa == 0)
            return false;
        memset((void *)PA_TO_VA(new_pa), 0, PAGE_SIZE);
        allocated_new = true;
    } else {
        return false;
    }

    if (!VmmMapRange(as, page_va, new_pa, PAGE_SIZE,
                       r->prot, r->memtype, r->owner, r->flags)) {
        if (allocated_new) {
            if (r->owner == VM_OWNER_SHARED && r->backing) {
                MemObject *mem = (MemObject *)r->backing;
                size_t page_index = (size_t)((page_va - r->vaddr_start) / PAGE_SIZE);
                if (page_index < mem->shm.page_count && mem->shm.page_addrs[page_index] == new_pa)
                    mem->shm.page_addrs[page_index] = 0;
            }
            PmmFreeFrame(new_pa);
        }
        return false;
    }

    return true;
}

AddressSpace *AddrspaceCreate(AsType type)
{
    if (!addrspace_cache.obj_size)
        KSlabInit(&addrspace_cache, "AddressSpace", sizeof(AddressSpace));
    AddressSpace *as = KSlabAlloc(&addrspace_cache);
    if (!as) {
        return NULL;
    }
    memset(as, 0, sizeof(*as));
    as->asid_token = (asid_token_t){0};

    as->pt_root_physaddr = arch_mmu_create_tables(type);

    if (as->pt_root_physaddr == 0) {
        KSlabFree(&addrspace_cache, as);
        return NULL;
    }

    if (type == ADDRSPACE_USER) {
        as->asid_token = asid_alloc();
        if (as->asid_token.asid == 0) {
            arch_mmu_free_tables(as->pt_root_physaddr, type);
            KSlabFree(&addrspace_cache, as);
            return NULL;
        }
    }

    if (!vm_region_vec_init(&as->regions)) {
        if (as->asid_token.asid != 0) {
            arch_mmu_flush_tlb_asid(as->asid_token.asid);
            asid_free(as->asid_token);
        }
        arch_mmu_free_tables(as->pt_root_physaddr, type);
        KSlabFree(&addrspace_cache, as);
        return NULL;
    }

    as->type = type;
    return as;
}

void AddrspaceDestroy(AddressSpace *as)
{
    if (!as) return;
    if (as == g_current_addrspace) {
        panic("Attempted to destroy active addrspace %p (asid=%u)",
              (void *)as, as->asid_token.asid);
        __builtin_unreachable();
    }

    /* Prevent stale translations from surviving ASID reuse. */
    if (as->asid_token.asid != 0)
        arch_mmu_flush_tlb_asid(as->asid_token.asid);

    for (uint32_t i = 0; i < as->regions.len; i++) {
        VirtMemRegion *r = vm_region_vec_get(&as->regions, i);
        if (!r)
            continue;
        VmmUnmapRange(as, r->vaddr_start, r->size, false);
    }

    if (as->asid_token.asid != 0)
        asid_free(as->asid_token);

    arch_mmu_free_tables(as->pt_root_physaddr, as->type);
    vm_region_vec_destroy(&as->regions);
    KSlabFree(&addrspace_cache, as);
}

bool VmmAddRegion(AddressSpace *restrict as, const VirtMemRegion *restrict region)
{
    if (!as || !region || region->size == 0) return false;

    VirtAddr new_start = region->vaddr_start;
    VirtAddr new_end   = new_start + region->size;

    uint32_t lo = 0, hi = as->regions.len;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) / 2);
        if (as->regions.data[mid].vaddr_start <= new_start)
            lo = mid + 1;
        else
            hi = mid;
    }
    uint32_t ins = lo;

    if (ins > 0) {
        VirtMemRegion *left = &as->regions.data[ins - 1];
        if (left->vaddr_start + left->size > new_start)
            return false;
    }
    if (ins < as->regions.len) {
        if (new_end > as->regions.data[ins].vaddr_start)
            return false;
    }

    if (as->regions.len >= as->regions.cap) {
        if (vm_region_vec_grow(&as->regions) < 0)
            return false;
    }

    memmove(&as->regions.data[ins + 1], &as->regions.data[ins],
            (as->regions.len - ins) * sizeof(VirtMemRegion));
    as->regions.data[ins] = *region;
    as->regions.len++;
    return true;
}

VirtAddr VmmFindFreeVa(const AddressSpace *as, VirtAddr lo, VirtAddr hi, size_t size)
{
    if (!as || size == 0 || lo >= hi || size > hi - lo)
        return 0;

    VirtAddr cand = lo;
    for (uint32_t i = 0; i < as->regions.len; i++) {
        const VirtMemRegion *r = &as->regions.data[i];
        VirtAddr r_start = r->vaddr_start;
        VirtAddr r_end = align_up(r_start + r->size, PAGE_SIZE);

        if (r_end <= cand)
            continue;
        if (r_start >= hi)
            break;
        if (r_start > cand && r_start - cand >= size)
            return cand;
        if (r_end >= hi)
            return 0;
        cand = r_end;
    }

    return (hi - cand >= size) ? cand : 0;
}

bool VmmRemoveRegion(AddressSpace *as, uintptr_t vaddr, size_t size)
{
    if (!as || size == 0) return false;

    VirtMemRegion *r = bsearch(&vaddr, as->regions.data, as->regions.len,
                              sizeof(VirtMemRegion), RegionCmpStart);
    if (!r || r->size != size)
        return false;

    VmmUnmapRange(as, vaddr, size, true);

    uint32_t idx = (uint32_t)(r - as->regions.data);
    memmove(r, r + 1, (as->regions.len - idx - 1) * sizeof(VirtMemRegion));
    as->regions.len--;
    return true;
}

bool VmmBuildPts(AddressSpace *as)
{
    if (!as) return false;

    for (uint32_t i = 0; i < as->regions.len; i++) {
        VirtMemRegion *r = vm_region_vec_get(&as->regions, i);
        if (!r) continue;
        if (r->flags & VM_FLAG_GUARD) continue;
        if (!VmmMapRange(as, r->vaddr_start, r->paddr_start, r->size,
                        r->prot, r->memtype, r->owner, r->flags))
            return false;
    }
    return true;
}
