/**
 * arch/mmu.h - Neutral MMU / page-table contract.
 *
 * The kernel VMM drives address spaces through this interface; the active
 * architecture implements the page-table format behind it (ARMv7-A short
 * descriptor: 2-level tables, 4 KB pages, 1 MB sections). Neutral types
 * (AddressSpace, MemProt, VirtMemType) come from kernel/mm/vmm/vmm.h.
 */

#ifndef ARCH_MMU_H
#define ARCH_MMU_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/mm/vmm/vmm.h"

/* Architecture section/large-page size (used by ioremap slot math in the VMM). */
#define SECTION_SIZE 0x100000u

/**
 * @brief Allocate and initialize a top-level page table.
 * @param type ADDRESS_SPACE_USER or ADDRESS_SPACE_KERNEL.
 * @return Physical address of the table, or 0 on failure.
 */
uintptr_t ArchMmuCreateTables(AddressSpaceType type);

/** @brief Free page tables for an address space. */
void ArchMmuFreeTables(uintptr_t ttbr_pa, AddressSpaceType type);

/**
 * @brief Map [va, va+size) -> [pa, pa+size) with the given protection/memtype.
 * @return true on success, false on allocation failure or bad arguments.
 */
bool ArchMmuMap(AddressSpace *as, uintptr_t va, uintptr_t pa, size_t size,
                  MemProt prot, VirtMemType memtype);

/** @brief Remove mappings over [va, va+size). */
bool ArchMmuUnmap(AddressSpace *as, uintptr_t va, size_t size, bool flush);

/** @brief Change protection over [va, va+size). */
bool ArchMmuProtect(AddressSpace *as, uintptr_t va, size_t size, MemProt prot);

/** @brief Enable the MMU using the given (kernel) address space. */
void ArchMmuEnable(AddressSpace *as);

/** @brief Switch the active user address space (context switch). */
void ArchMmuSwitch(AddressSpace *as);

/** @brief Invalidate the entire TLB. */
void ArchMmuFlushTlb(void);

/** @brief Invalidate TLB entries for a single ASID. */
void ArchMmuFlushTlbAsid(uint8_t asid);

/** @brief Invalidate the TLB entry for a single virtual address. */
void ArchMmuFlushTlbVa(uintptr_t va);

void ArchMmuFlushTlbVaAsid(uintptr_t va, uint8_t asid);

/**
 * @brief Walk page tables to translate a VA to its PA.
 * @return Physical address, or 0 if unmapped.
 */
uintptr_t ArchMmuTranslate(uintptr_t ttbr_pa, uintptr_t va);

/** @brief Unmap a single page. */
bool ArchMmuUnmapPage(AddressSpace *as, uintptr_t va);

/** @brief Free all user-mapped backing pages, leaving page tables intact. */
void ArchMmuFreeUserPages(AddressSpace *as);

/** @brief Initialize the kernel translation base (TTBR1 on ARM) for user mode. */
void ArchMmuInitTtbr1(AddressSpace *as);

/* Inline, architecture-private helpers (e.g. ArchRelocateStacks). */
#include <arch_impl/mmu.h>

#endif // ARCH_MMU_H
