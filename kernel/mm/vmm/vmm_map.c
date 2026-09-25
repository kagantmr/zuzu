/* Page-table-level mapping mechanics: map/unmap/protect a VA range,
 * activate an address space, and validate+demand-fault a user access. */

#include "vmm_internal.h"

#include "kernel/mm/pmm/pmm.h"

#include <arch/mmu.h>
#include <stdint.h>

bool VmmMapRange(AddressSpace *as, VirtAddr va, PhysAddr pa, size_t size,
                   MemProt prot, VirtMemType memtype, VirtMemOwner owner, VirtMemFlags flags) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % 0x1000) != 0) return false;
    if ((pa % 0x1000) != 0) return false;

    // check overflow
    if (va > UINTPTR_MAX - size) return false;

    if (as->type == ADDRSPACE_USER && (prot & PROT_WRITE) && (prot & PROT_EXEC))
        return false;

    if (as->type == ADDRSPACE_USER) {
        // For user address spaces, enforce canonical user VA range [0, USER_VA_TOP).
        // end is exclusive, so end == USER_VA_TOP is valid.
        if (va >= USER_VA_TOP || va + size > USER_VA_TOP) {
            return false;
        }
    }

    (void)owner;
    (void)flags;

    // Delegate to arch layer (handles ownership and flags at the architecture level)
    return arch_mmu_map(as, va, pa, size, prot, memtype);
}

bool VmmUnmapRange(AddressSpace *as, VirtAddr va, size_t size, bool flush) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;    // page granularity
    if ((size % PAGE_SIZE) != 0) return false;  // page granularity

    return arch_mmu_unmap(as, va, size, flush);
}

bool VmmProtectPage(AddressSpace *as, VirtAddr va, size_t size, MemProt new_prot)
{
    if (!as || size == 0) return false;

    VirtMemRegion *r = VmmFindRegion(as, va);
    if (!r) return false;                          /* no region → refuse */
    if (va + size > r->vaddr_start + r->size)      /* must not span regions */
        return false;
    if (r->memtype == VM_MEM_DEVICE && (new_prot & PROT_EXEC))
        return false;                              /* no executable MMIO */
    if (r->flags & VM_FLAG_PINNED)                 /* tcb_page/syspage */
        return false;

    if (!arch_mmu_protect(as, va, size, new_prot))
        return false;

    r->prot = new_prot;                            /* keep region truth in sync */
    return true;
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

void VmmActivateAddrspace(AddressSpace *as) {
    if (!as) return;
    if (as == g_current_addrspace) return;

    if (!g_mmu_enabled) {
        arch_mmu_enable(as);
        g_mmu_enabled = true;
    } else {
        arch_mmu_switch(as);
    }

    g_current_addrspace = as;
}
