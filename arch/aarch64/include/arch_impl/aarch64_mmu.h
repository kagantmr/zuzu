/**
 * @file
 * @brief AArch64 VMSAv8-64 translation table descriptor format
 * 
 */

#ifndef ARCH_AARCH64_IMPL_AARCH64_MMU_H
#define ARCH_AARCH64_IMPL_AARCH64_MMU_H

#include <stdbool.h>
#include <stdint.h>

/** @brief A translation table entry at any level. */
typedef uint64_t AArch64Pte;

/** @brief Size of the largest block a single top-level entry maps (an ARMv7 section, 1 MiB). */
#define SECTION_SIZE 0x200000UL

/** @brief Entries in one translation table. */
#define PTE_ENTRIES 512U
/** @brief Index of the last translation level, where entries map 4 KiB pages. */
#define PTE_LEVEL_LAST 3
/** @brief Level whose blocks ArchMmuMap() uses for kernel mappings: 2 MiB. */
#define PTE_LEVEL_BLOCK 2

/** @brief Entry is in use; any access through an entry without it faults. */
#define PTE_VALID (1UL << 0)
/** @brief At levels 0-2 the entry points to a table, at level 3 it maps a page; clear means a
 * block. */
#define PTE_TABLE_OR_PAGE (1UL << 1)

/** @brief AttrIndx[2:0], bits [4:2]: the MAIR_EL1 slot that gives the memory type. */
#define PTE_ATTRINDX(i) ((uint64_t)(i) << 2)
/** @brief Mask of the AttrIndx field. */
#define PTE_ATTRINDX_MASK PTE_ATTRINDX(7)

/** @brief MAIR_EL1 slots set up by the boot code. */
enum {
    PTE_ATTRINDX_DEVICE = 0,    /**< Device-nGnRnE (MAIR byte 0x00). */
    PTE_ATTRINDX_NORMAL = 1,    /**< Normal, write-back cacheable (MAIR byte 0xFF). */
    PTE_ATTRINDX_NORMAL_NC = 2, /**< Normal, non-cacheable (MAIR byte 0x44). */
};

/** @brief AP[1], bit 6: EL0 may access the memory. */
#define PTE_AP_EL0 (1UL << 6)
/** @brief AP[2], bit 7: read-only at every level that may access it. */
#define PTE_AP_RO (1UL << 7)

/** @brief SH[1:0] = 0b11, bits [9:8]: inner shareable. */
#define PTE_SH_INNER (3UL << 8)
/** @brief Access flag, bit 10; zuzu sets it on every mapping so first accesses do not fault. */
#define PTE_AF (1UL << 10)
/** @brief Not global, bit 11: the TLB entry is tagged with the current ASID. */
#define PTE_NG (1UL << 11)

/** @brief Output address, bits [47:12]: the next table's or the page's physical address. */
#define PTE_ADDR_MASK 0x0000FFFFFFFFF000UL

/** @brief Privileged execute-never, bit 53: EL1 may not execute the memory. */
#define PTE_PXN (1UL << 53)
/** @brief Unprivileged execute-never, bit 54: EL0 may not execute the memory. */
#define PTE_UXN (1UL << 54)

/** @brief Bits that a protection change may rewrite in a live entry without break-before-make. */
#define PTE_PERM_MASK (PTE_AP_EL0 | PTE_AP_RO | PTE_PXN | PTE_UXN | PTE_NG)

/** @brief ASID field of a TLBI operand, bits [63:48]. */
#define TLBI_ASID(asid) ((uint64_t)(asid) << 48)
/** @brief VA field of a TLBI operand: VA bits [55:12] in operand bits [43:0]. */
#define TLBI_VA(va) (((uint64_t)(va) >> 12) & 0xFFFFFFFFFFFUL)

/**
 * @brief Bytes one entry at @p level maps: 512 GiB, 1 GiB, 2 MiB or 4 KiB.
 *
 * @param[in] level Translation level, 0 to 3.
 * @return The size in bytes.
 */
static inline uint64_t PteLevelSize(int level) { return 1UL << (39 - 9 * level); }

/**
 * @brief Index of the entry that translates @p va in a table at @p level.
 *
 * @param[in] va    Virtual address.
 * @param[in] level Translation level, 0 to 3.
 * @return The index, 0 to 511.
 */
static inline unsigned PteIndex(uint64_t va, int level)
{
    return (unsigned)((va >> (39 - 9 * level)) & (PTE_ENTRIES - 1));
}

/**
 * @brief Tells whether an entry at @p level is a valid table pointer.
 *
 * @param[in] e     Entry.
 * @param[in] level Translation level.
 * @return true at levels 0-2 when the entry is valid and has the table bit.
 */
static inline bool PteIsTable(AArch64Pte e, int level)
{
    return level < PTE_LEVEL_LAST &&
           (e & (PTE_VALID | PTE_TABLE_OR_PAGE)) == (PTE_VALID | PTE_TABLE_OR_PAGE);
}

#endif // ARCH_AARCH64_IMPL_AARCH64_MMU_H
