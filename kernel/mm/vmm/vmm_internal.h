#ifndef ZUZU_VMM_INTERNAL_H
#define ZUZU_VMM_INTERNAL_H

/* Cross-TU glue shared by vmm_region.c, vmm_map.c, vmm_boot.c, and
 * ioremap.c. Not part of the public vmm.h API -- do not include from
 * outside kernel/mm/vmm/. */

#include "vmm.h"

extern AddressSpace *g_kernel_as;
extern AddressSpace *g_current_addrspace;
extern bool g_mmu_enabled;

VirtMemRegion *VmmFindRegion(AddressSpace *as, uintptr_t va);

#endif /* ZUZU_VMM_INTERNAL_H */
