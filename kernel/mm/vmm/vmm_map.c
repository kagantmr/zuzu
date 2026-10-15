#include "vmm_internal.h"

#include "kernel/mm/pmm/pmm.h"

#include <arch/mmu.h>
#include <stdint.h>

bool VmmMapRange(AddressSpace *as, VirtAddr va, PhysAddr pa, size_t size, MemProt prot,
                 VirtMemType memtype)
{
    if (!as)
        return false;
    if (size == 0)
        return false;
    if ((va % 0x1000) != 0)
        return false;
    if ((pa % 0x1000) != 0)
        return false;

    if (va > UINTPTR_MAX - size)
        return false;

    if (as->type == ADDRESS_SPACE_USER && (prot & PROT_WRITE) && (prot & PROT_EXEC))
        return false;

    if (as->type == ADDRESS_SPACE_USER) {
        if (va >= USER_VA_TOP || va + size > USER_VA_TOP) {
            return false;
        }
    }

    return ArchMmuMap(as, va, pa, size, prot, memtype);
}

bool VmmUnmapRange(AddressSpace *as, VirtAddr va, size_t size, bool flush)
{
    if (!as)
        return false;
    if (size == 0)
        return false;
    if ((va % PAGE_SIZE) != 0)
        return false;
    if ((size % PAGE_SIZE) != 0)
        return false;

    return ArchMmuUnmap(as, va, size, flush);
}

bool VmmProtectPage(AddressSpace *as, VirtAddr va, size_t size, MemProt new_prot)
{
    if (!as || size == 0)
        return false;

    VirtMemRegion *r = VmmFindRegion(as, va);
    if (!r)
        return false;
    if (va + size > r->vaddr_start + r->size)
        return false;
    if (r->memtype == VM_MEM_DEVICE && (new_prot & PROT_EXEC))
        return false;
    if (r->memtype == VM_MEM_NORMAL_NC && (new_prot & PROT_EXEC))
        return false;
    if (r->flags & VM_FLAG_PINNED)
        return false;

    if (!ArchMmuProtect(as, va, size, new_prot))
        return false;

    r->prot = new_prot;
    return true;
}

void VmmActivateAddressSpace(AddressSpace *as)
{
    if (!as)
        return;
    if (as == g_current_addrspace)
        return;

    if (!g_mmu_enabled) {
        ArchMmuEnable(as);
        g_mmu_enabled = true;
    } else {
        ArchMmuSwitch(as);
    }

    g_current_addrspace = as;
}
