/**
 * arch/mmu.h - Neutral MMU / page-table contract.
 *
 * The kernel VMM drives address spaces through this interface; the active
 * architecture implements the page-table format behind it (ARMv7-A short
 * descriptor: 2-level tables, 4 KB pages, 1 MB sections). Neutral types
 * (addrspace_t, vm_prot_t, vm_memtype_t) come from kernel/mm/vmm.h.
 */

#ifndef ZUZU_ARCH_MMU_H
#define ZUZU_ARCH_MMU_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/mm/vmm.h"

/* Architecture section/large-page size (used by ioremap slot math in the VMM). */
#define SECTION_SIZE 0x100000

/**
 * @brief Allocate and initialize a top-level page table.
 * @param type ADDRSPACE_USER or ADDRSPACE_KERNEL.
 * @return Physical address of the table, or 0 on failure.
 */
uintptr_t arch_mmu_create_tables(addrspace_type_t type);

/** @brief Free page tables for an address space. */
void arch_mmu_free_tables(uintptr_t ttbr0_pa, addrspace_type_t type);

/**
 * @brief Map [va, va+size) -> [pa, pa+size) with the given protection/memtype.
 * @return true on success, false on allocation failure or bad arguments.
 */
bool arch_mmu_map(addrspace_t *as, uintptr_t va, uintptr_t pa, size_t size,
                  vm_prot_t prot, vm_memtype_t memtype);

/** @brief Remove mappings over [va, va+size). */
bool arch_mmu_unmap(addrspace_t *as, uintptr_t va, size_t size);

/** @brief Change protection over [va, va+size). */
bool arch_mmu_protect(addrspace_t *as, uintptr_t va, size_t size, vm_prot_t prot);

/** @brief Enable the MMU using the given (kernel) address space. */
void arch_mmu_enable(addrspace_t *as);

/** @brief Switch the active user address space (context switch). */
void arch_mmu_switch(addrspace_t *as);

/** @brief Invalidate the entire TLB. */
void arch_mmu_flush_tlb(void);

/** @brief Invalidate TLB entries for a single ASID. */
void arch_mmu_flush_tlb_asid(uint8_t asid);

/** @brief Invalidate the TLB entry for a single virtual address. */
void arch_mmu_flush_tlb_va(uintptr_t va);

/**
 * @brief Walk page tables to translate a VA to its PA.
 * @return Physical address, or 0 if unmapped.
 */
uintptr_t arch_mmu_translate(uintptr_t ttbr0_pa, uintptr_t va);

/** @brief Unmap a single page. */
bool arch_mmu_unmap_page(addrspace_t *as, uintptr_t va);

/** @brief Free all user-mapped backing pages, leaving page tables intact. */
void arch_mmu_free_user_pages(addrspace_t *as);

/** @brief Initialize the kernel translation base (TTBR1 on ARM) for user mode. */
void arch_mmu_init_ttbr1(addrspace_t *as);

/** @brief Issue memory/instruction barriers after MMU state changes. */
void arch_mmu_barrier(void);

/* Inline, architecture-private helpers (e.g. arch_relocate_stacks). */
#include <arch_impl/mmu.h>

#endif // ZUZU_ARCH_MMU_H
