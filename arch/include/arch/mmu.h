/**
 * @file mmu.h
 * @brief Architecture neutral MMU definitions
 */

#ifndef ARCH_MMU_H
#define ARCH_MMU_H

#include "kernel/mm/vmm/vmm.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief Size of the largest block a single top-level entry maps (an ARMv7 section, 1 MiB). */
#define SECTION_SIZE 0x100000u

/**
 * @brief Create page tables for a given address space type and return its base.
 *
 * The tables start empty: nothing is mapped.
 *
 * @param[in] type The type of the address space
 *
 * @return Physical address of the page table root, or 0 when out of memory.
 */
uintptr_t ArchMmuCreateTables(AddressSpaceType type);

/**
 * @brief Free page tables for a given address space type.
 *
 * Frees the root table and every lower-level table it points to, but not the
 * pages they map; see ArchMmuFreeUserPages(). A @p ttbr_pa of 0 is ignored.
 *
 * @param[in] ttbr_pa Base address of the root page table.
 * @param[in] type The type of the address space.
 *
 */
void ArchMmuFreeTables(uintptr_t ttbr_pa, AddressSpaceType type);

/**
 * @brief Maps a physically contiguous range.
 *
 * A range whose @p va, @p pa and @p size are all section-aligned is mapped with
 * sections; otherwise it must be page-aligned and is mapped page by page. If a
 * page cannot be mapped, the pages mapped so far are unmapped again. No TLB
 * maintenance is done: the range is expected to be unmapped beforehand.
 *
 * @param[in,out] as      Address space.
 * @param[in]     va      Virtual start.
 * @param[in]     pa      Physical start.
 * @param[in]     size    Size in bytes.
 * @param[in]     prot    Protection.
 * @param[in]     memtype Memory type.
 * @return false if the range is misaligned or outside the space, or a table could not be
 *         allocated.
 */
bool ArchMmuMap(AddressSpace *as, uintptr_t va, uintptr_t pa, size_t size, MemProt prot,
                VirtMemType memtype);

/**
 * @brief Unmaps a range. The pages themselves are not freed.
 *
 * @param[in,out] as    Address space.
 * @param[in]     va    Virtual start; page-aligned.
 * @param[in]     size  Size in bytes; page-aligned.
 * @param[in]     flush Whether to remove the range from the TLB.
 * @return true if anything was mapped there.
 */
bool ArchMmuUnmap(AddressSpace *as, uintptr_t va, size_t size, bool flush);

/**
 * @brief Changes the protection of the mapped pages in a range, then flushes the space's TLB
 * entries.
 *
 * A section inside the range changes as a whole. Unmapped pages are skipped.
 *
 * @param[in,out] as   Address space.
 * @param[in]     va   Virtual start.
 * @param[in]     size Size in bytes.
 * @param[in]     prot New protection.
 * @return true if any mapping changed.
 */
bool ArchMmuProtect(AddressSpace *as, uintptr_t va, size_t size, MemProt prot);

/**
 * @brief Loads an address space's tables and turns the MMU on.
 *
 * @param[in] as Address space; ignored if NULL or without tables.
 */
void ArchMmuEnable(AddressSpace *as);

/**
 * @brief Switches the CPU to an address space, renewing its ASID if the ASID generation has
 * moved on.
 *
 * @param[in,out] as Address space; ignored if NULL or without tables.
 */
void ArchMmuSwitch(AddressSpace *as);

/**
 * @brief Invalidates the whole TLB.
 */
void ArchMmuFlushTlb(void);

/**
 * @brief Invalidates every TLB entry of one ASID.
 *
 * @param[in] asid ASID; 0 invalidates the whole TLB.
 */
void ArchMmuFlushTlbAsid(uint8_t asid);

/**
 * @brief Invalidates the TLB entries for one page in every ASID.
 *
 * @param[in] va Any address in the page.
 */
void ArchMmuFlushTlbVa(uintptr_t va);

/**
 * @brief Invalidates the TLB entry for one page of one ASID.
 *
 * @param[in] va   Any address in the page.
 * @param[in] asid ASID.
 */
void ArchMmuFlushTlbVaAsid(uintptr_t va, uint8_t asid);

/**
 * @brief Walks a table in software to find what a virtual address maps to.
 *
 * @param[in] ttbr_pa Physical address of the root table.
 * @param[in] va      Virtual address.
 * @return The physical address, or 0 if @p va is not mapped.
 */
uintptr_t ArchMmuTranslate(uintptr_t ttbr_pa, uintptr_t va);

/**
 * @brief Clears one page mapping, without TLB maintenance.
 *
 * @param[in,out] as Address space.
 * @param[in]     va Virtual address of the page.
 * @return false if the page was not mapped by a page-level entry.
 */
bool ArchMmuUnmapPage(AddressSpace *as, uintptr_t va);

/**
 * @brief Frees the anonymous memory still mapped in a user space, before its tables are freed.
 *
 * Pages that belong to memory objects, device mappings and the syspage are left alone.
 *
 * @param[in] as Address space; ignored if NULL.
 */
void ArchMmuFreeUserPages(AddressSpace *as);

/**
 * @brief Makes the kernel's tables the upper-half (TTBR1) tables and splits the address space
 * at 2 GiB.
 *
 * @param[in] as The kernel's address space.
 */
void ArchMmuInitTtbr1(AddressSpace *as);

#include <arch_impl/mmu.h>

#endif // ARCH_MMU_H
