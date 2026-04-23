/**
 * mmu.h - ARM MMU interface and definitions.
 * The ARMv7 MMU uses a 2-level paging scheme, with a 4 KB page size and 1 MB section size (and optional 16MB supersections that zuzuOS doesn't use).
 * This header defines the interface for managing page tables, mapping/unmapping virtual memory, and handling ASIDs. 
 * The implementation will need to handle the specifics of ARM's page table formats, including the encoding of permissions and memory types.
 */

#ifndef ARCH_ARM_MMU_MMU_H
#define ARCH_ARM_MMU_MMU_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/mm/vmm.h"

/**
 * @brief Allocate and initialize an L1 page table.
 * @return Physical address of the L1 table, or 0 on failure.
 */
uintptr_t arch_mmu_create_tables(void);

/**
 * @brief Free page tables.
 * @param ttbr0_pa Physical address of the L1 table.
 */
void arch_mmu_free_tables(uintptr_t ttbr0_pa, addrspace_type_t type);

/**
 * @brief Encode mappings into ARM L1/L2 tables.
 * @param as Address space (contains TTBR0 PA).
 * @param va Virtual address.
 * @param pa Physical address.
 * @param size Size in bytes (should be page-aligned or section-aligned).
 * @param prot Protection flags (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC | ...).
 * @param memtype VM_MEM_NORMAL or VM_MEM_DEVICE.
 * @return true on success, false on L2 allocation failure or other error.
 */
bool arch_mmu_map(addrspace_t *as, uintptr_t va, uintptr_t pa, size_t size,
                  vm_prot_t prot, vm_memtype_t memtype);

/**
 * @brief Remove mappings from page tables.
 * @param as Address space.
 * @param va Virtual address.
 * @param size Size in bytes.
 * @return true on success, false if not found.
 */
bool arch_mmu_unmap(addrspace_t *as, uintptr_t va, size_t size);

/**
 * @brief Change permissions on page-table entries.
 * @param as Address space.
 * @param va Virtual address.
 * @param size Size in bytes.
 * @param prot New protection flags.
 * @return true on success, false if not found.
 */
bool arch_mmu_protect(addrspace_t *as, uintptr_t va, size_t size, vm_prot_t prot);

/**
 * @brief Enable the MMU (turn on virtual memory).
 * @param as Kernel address space (contains TTBR0 PA).
 */
void arch_mmu_enable(addrspace_t *as);

/**
 * @brief Disable the MMU (rare; mostly for shutdown).
 * Disables virtual memory and returns to physical addressing.
 */
void arch_mmu_disable(void);

/**
 * @brief Switch TTBR0 to another address space (context switch).
 * @param as Address space to activate.
 */
void arch_mmu_switch(addrspace_t *as);

/**
 * @brief Invalidate the entire TLB.
 */
void arch_mmu_flush_tlb(void);

/**
 * @brief Invalidate TLB entries for a specific ASID.
 * @param asid Address-space identifier (0-255).
 */
void arch_mmu_flush_tlb_asid(uint8_t asid);

/**
 * @brief Invalidate TLB entry for a single virtual address.
 * @param va Virtual address.
 */
void arch_mmu_flush_tlb_va(uintptr_t va);

/**
 * @brief Translate a virtual address to physical address using page tables.
 * @param ttbr0_pa Physical address of the L1 page table.
 * @param va Virtual address to translate.
 * @return Physical address (page-aligned base) if mapping exists.
 *         Returns 0 if VA is not mapped, or on error.
 */
uintptr_t arch_mmu_translate(uintptr_t ttbr0_pa, uintptr_t va);

/**
 * @brief Allocate a new L2 page table.
 * @return Physical address of the allocated L2 table, or 0 on failure.
 */
uintptr_t arch_mmu_alloc_l2_table(void);

/**
 * @brief Make an L1 page table entry pointing to the given L2 table physical address.
 * @param l2_pa PA of the table to make an entry to
 * @return The entry itself
 */
uint32_t arch_mmu_make_l1_pte(uintptr_t l2_pa);

/**
 * Make an L2 page table entry for the given physical address and permissions.
 * @param pa Physical address to translate
 * @param memtype Enum for memory type (MMIO, user, kernel)
 * @param prot Access privilege
 * @return The correct page table entry
 */
uint32_t arch_mmu_make_l2_pte(uintptr_t pa, vm_memtype_t memtype, vm_prot_t prot);

/**
 * Map a single page at the given virtual address to the given physical address with specified permissions.
 * @param as Address space pointer
 * @param va Virtual address to map at
 * @param pa Physical address to map
 * @param memtype Enum for memory type (MMIO, user, kernel)
 * @param prot Access privilege
 * @return true if success, false if fail
 */
bool arch_mmu_map_page(addrspace_t *as, uintptr_t va, uintptr_t pa, vm_memtype_t memtype, vm_prot_t prot);

/**
 * Unmap a single page at the given virtual address.
 * @param as Address space ptr
 * @param va Virtual address to unmap
 * @return true if success, false if fail
 */
bool arch_mmu_unmap_page(addrspace_t *as, uintptr_t va);

/**
 * Create and initialize page tables for a new user address space.
 * Allocates and zeroes an L1 table suitable for TTBR0.
 * @return Pointer to page tables
 */
uintptr_t arch_mmu_create_user_tables(void);

/**
 * Free all user-mapped pages in the given address space, but keep page tables intact.
 * @param as Address space pointer
 */
void arch_mmu_free_user_pages(addrspace_t *as);

/**
 * @brief Initialize TTBR1 for the given address space.
 * @param as Address space to switch to TTBR1 in preparation for user mode.
 */
void arch_mmu_init_ttbr1(addrspace_t *as);

/**
 * @brief Issue memory barriers (ISB, DSB).
 * Used after MMU state changes to ensure visibility.
 */
void arch_mmu_barrier(void);

#endif // ARCH_ARM_MMU_MMU_H
