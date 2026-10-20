/**
 * @file vmm_internal.h
 * @brief State and helpers shared between the VMM implementation files.
 */

#ifndef KERNEL_MM_VMM_VMM_INTERNAL_H
#define KERNEL_MM_VMM_VMM_INTERNAL_H

#include "vmm.h"

/** @brief The kernel address space, created by VmmBootstrap(). */
extern AddressSpace *g_kernel_as;
/** @brief The address space currently installed in the MMU. */
extern AddressSpace *g_current_addrspace;
/** @brief Whether the MMU has been switched on with a kernel-owned address space. */
extern bool g_mmu_enabled;

/**
 * @brief Find the region that contains an address.
 *
 * @param[in] as The space to search.
 * @param[in] va The address to look up.
 * @return The containing region, or NULL if there is none.
 */
VirtMemRegion *VmmFindRegion(AddressSpace *as, uintptr_t va);

#endif /* KERNEL_MM_VMM_VMM_INTERNAL_H */
