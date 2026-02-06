#ifndef ARCH_ARM_MMU_MMU_H
#define ARCH_ARM_MMU_MMU_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/vmm/vmm.h"

/**
 * @brief Allocate and initialize an L1 page table.
 * @return Physical address of the L1 table, or 0 on failure.
 * Responsibilities:
 *   - allocate 16 KB (4096 entries × 4 bytes)
 *   - clear to zero or set up faults
 *   - return physical address for TTBR0
 */
uintptr_t arch_mmu_create_tables(void);

/**
 * @brief Free page tables (later phase).
 * @param ttbr0_pa Physical address of the L1 table.
 * Responsibilities:
 *   - free all L2 tables
 *   - free L1 table
 *   - release physical memory
 */
void arch_mmu_free_tables(uintptr_t ttbr0_pa);

/**
 * @brief Encode mappings into ARM L1/L2 tables.
 * @param as Address space (contains TTBR0 PA).
 * @param va Virtual address.
 * @param pa Physical address.
 * @param size Size in bytes (should be page-aligned or section-aligned).
 * @param prot Protection flags (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC | ...).
 * @param memtype VM_MEM_NORMAL or VM_MEM_DEVICE.
 * @return true on success, false on L2 allocation failure or other error.
 * Responsibilities:
 *   - choose section (1 MB) vs page (4 KB) mapping
 *   - allocate L2 tables if needed
 *   - encode AP (access permissions) bits
 *   - encode XN (execute-never) bit
 *   - encode TEX/C/B (memory type) bits
 *   - write to page table
 */
bool arch_mmu_map(addrspace_t* as, uintptr_t va, uintptr_t pa, size_t size,
                  vm_prot_t prot, vm_memtype_t memtype);

/**
 * @brief Remove mappings from page tables.
 * @param as Address space.
 * @param va Virtual address.
 * @param size Size in bytes.
 * @return true on success, false if not found.
 * Responsibilities:
 *   - clear L1/L2 entries
 *   - optionally free empty L2 tables
 *   - invalidate TLB
 */
bool arch_mmu_unmap(addrspace_t* as, uintptr_t va, size_t size);

/**
 * @brief Change permissions on page-table entries.
 * @param as Address space.
 * @param va Virtual address.
 * @param size Size in bytes.
 * @param prot New protection flags.
 * @return true on success, false if not found.
 */
bool arch_mmu_protect(addrspace_t* as, uintptr_t va, size_t size, vm_prot_t prot);

/**
 * @brief Enable the MMU (turn on virtual memory).
 * @param as Kernel address space (contains TTBR0 PA).
 * Responsibilities:
 *   - write TTBR0 register
 *   - configure domain access control (DACR)
 *   - enable MMU control register bit
 *   - issue barriers (ISB/DSB) and TLB invalidate
 *   - may require code relocation or special asm
 */
void arch_mmu_enable(addrspace_t* as);

/**
 * @brief Disable the MMU (rare; mostly for shutdown).
 * Disables virtual memory and returns to physical addressing.
 */
void arch_mmu_disable(void);

/**
 * @brief Switch TTBR0 to another address space (context switch).
 * @param as Address space to activate.
 * Responsibilities:
 *   - write TTBR0
 *   - invalidate TLB
 *   - barriers
 */
void arch_mmu_switch(addrspace_t* as);

/**
 * @brief Invalidate the entire TLB.
 */
void arch_mmu_flush_tlb(void);

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
 *
 * Behavior:
 *   - Walks the L1 and L2 page tables to find the descriptor for VA
 *   - Returns the physical page base (aligned to page/section boundary)
 *   - Does NOT access the page; purely a table lookup
 *   - Used for discovering which physical pages belong to a region
 *
 * Note: Caller must ensure page tables are not being modified concurrently.
 */
uintptr_t arch_mmu_translate(uintptr_t ttbr0_pa, uintptr_t va);

uintptr_t arch_mmu_alloc_l2_table(void);
uint32_t arch_mmu_make_l1_pte(uintptr_t l2_pa);
uint32_t arch_mmu_make_l2_pte(uintptr_t pa, vm_memtype_t memtype);
bool arch_mmu_map_page(addrspace_t *as, uintptr_t va, uintptr_t pa, vm_memtype_t memtype);
bool arch_mmu_unmap_page(addrspace_t *as, uintptr_t va);

/**
 * @brief Issue memory barriers (ISB, DSB).
 * Used after MMU state changes to ensure visibility.
 */
void arch_mmu_barrier(void);

#endif // ARCH_ARM_MMU_MMU_H
