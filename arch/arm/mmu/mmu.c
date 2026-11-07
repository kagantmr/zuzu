/**
 * @file
 * @brief ARMv7-A translation tables: creating, mapping, protecting and tearing down.
 */

#include "kernel/mm/pmm/pmm.h"
#include "l2_pool.h"
#include "types.h"
#include <arch/asid.h>
#include <arch/barrier.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <arch_impl/armv7_mmu.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "kernel/layout.h"

#include BOARD_BOOT_H

/** @brief The kernel's physical memory layout, filled in during early boot. */
extern RamLayout kernel_layout;

/** @brief Unmaps up to this many pages flush them one by one; larger ones flush the whole ASID. */
#define UNMAP_TLBI_PAGE_THRESHOLD 16U

/**
 * @brief Maps one 4 KiB page, allocating an L2 table or splitting a section as needed.
 *
 * @param[in] as      Address space.
 * @param[in] va      Virtual address.
 * @param[in] pa      Physical address.
 * @param[in] memtype Memory type.
 * @param[in] prot    Protection.
 * @return false if @p va is out of range or no L2 table could be allocated.
 */
static bool MmuMapPage(AddressSpace *as, uintptr_t va, uintptr_t pa, VirtMemType memtype,
                           MemProt prot);

/**
 * @brief Entries in an L1 table of the given kind.
 *
 * @param[in] type User or kernel.
 * @return 2048 for user tables, 4096 for the kernel's.
 */
static inline size_t l1_entry_count(AddressSpaceType type)
{
    return (type == ADDRESS_SPACE_USER) ? L1_ENTRIES_USER : L1_ENTRIES_KERNEL;
}

/**
 * @brief Size of an L1 table of the given kind.
 *
 * @param[in] type User or kernel.
 * @return Size in bytes.
 */
static inline size_t l1_table_bytes(AddressSpaceType type)
{
    return l1_entry_count(type) * L1_DESC_BYTES;
}

/**
 * @brief Pages taken by an L1 table of the given kind.
 *
 * @param[in] type User or kernel.
 * @return Number of pages.
 */
static inline size_t l1_table_pages(AddressSpaceType type)
{
    return l1_table_bytes(type) / PAGE_SIZE;
}

/**
 * @brief AP[1:0] bits for a protection.
 *
 * @param[in] prot Protection.
 * @return AP_USER_RW, AP_USER_RO or AP_KERNEL_RW.
 */
static inline uint32_t ap_bits(MemProt prot)
{
    if (prot & VM_PROT_USER)
        return (prot & PROT_WRITE) ? AP_USER_RW : AP_USER_RO;
    return AP_KERNEL_RW;
}

/**
 * @brief Builds a section descriptor.
 *
 * User mappings are not-global; mappings without PROT_EXEC are execute-never.
 *
 * @param[in] pa      Physical address of the section.
 * @param[in] prot    Protection.
 * @param[in] memtype Memory type.
 * @return The descriptor.
 */
static uint32_t l1_section_desc(uintptr_t pa, MemProt prot, VirtMemType memtype)
{
    uint32_t e = (uint32_t)(pa & L1_SECTION_BASE_MASK) | L1_SECTION_TAG;

    e |= ap_bits(prot) << L1_SECT_AP_SHIFT;
    if (!(prot & PROT_EXEC))
        e |= MMU_BIT(L1_SECT_XN_BIT);
    if (prot & VM_PROT_USER)
        e |= MMU_BIT(L1_SECT_NG_BIT);
    switch (memtype) {
    case VM_MEM_DEVICE:
        e |= L1_SECT_ATTR_DEVICE;
        break;
    case VM_MEM_NORMAL_NC:
        e |= L1_SECT_ATTR_NORMAL_NC | MMU_BIT(L1_SECT_S_BIT);
        break;
    case VM_MEM_NORMAL:
    default:
        e |= L1_SECT_ATTR_NORMAL | MMU_BIT(L1_SECT_S_BIT);
        break;
    }

    return e;
}

/**
 * @brief Builds a small page descriptor, like l1_section_desc() does for sections.
 *
 * @param[in] pa      Physical address of the page.
 * @param[in] prot    Protection.
 * @param[in] memtype Memory type.
 * @return The descriptor.
 */
static uint32_t l2_page_desc(uintptr_t pa, MemProt prot, VirtMemType memtype)
{
    uint32_t e = (uint32_t)(pa & L2_SMALL_BASE_MASK) | L2_SMALL_TAG;

    e |= ap_bits(prot) << L2_PAGE_AP_SHIFT;
    if (!(prot & PROT_EXEC))
        e |= MMU_BIT(L2_PAGE_XN_BIT);
    if (prot & VM_PROT_USER)
        e |= MMU_BIT(L2_PAGE_NG_BIT);
    switch (memtype) {
    case VM_MEM_DEVICE:
        e |= L2_PAGE_ATTR_DEVICE;
        break;
    case VM_MEM_NORMAL_NC:
        e |= L2_PAGE_ATTR_NORMAL_NC | MMU_BIT(L2_PAGE_S_BIT);
        break;
    case VM_MEM_NORMAL:
    default:
        e |= L2_PAGE_ATTR_NORMAL | MMU_BIT(L2_PAGE_S_BIT);
        break;
    }

    return e;
}

/**
 * @brief Replaces the access permissions and execute-never bit of a section descriptor.
 *
 * @param[in] e    Descriptor.
 * @param[in] prot New protection.
 * @return The updated descriptor.
 */
static uint32_t l1_section_set_prot(uint32_t e, MemProt prot)
{
    e &= ~((AP_MASK << L1_SECT_AP_SHIFT) | MMU_BIT(L1_SECT_AP2_BIT) | MMU_BIT(L1_SECT_XN_BIT));
    e |= ap_bits(prot) << L1_SECT_AP_SHIFT;
    if (!(prot & PROT_EXEC))
        e |= MMU_BIT(L1_SECT_XN_BIT);
    return e;
}

/**
 * @brief Replaces the access permissions and execute-never bit of a small page descriptor.
 *
 * @param[in] e    Descriptor.
 * @param[in] prot New protection.
 * @return The updated descriptor.
 */
static uint32_t l2_page_set_prot(uint32_t e, MemProt prot)
{
    e &= ~((AP_MASK << L2_PAGE_AP_SHIFT) | MMU_BIT(L2_PAGE_AP2_BIT) | MMU_BIT(L2_PAGE_XN_BIT));
    e |= ap_bits(prot) << L2_PAGE_AP_SHIFT;
    if (!(prot & PROT_EXEC))
        e |= MMU_BIT(L2_PAGE_XN_BIT);
    return e;
}

/**
 * @brief TTBR value for a table: its address plus the walk attributes.
 *
 * @param[in] ttbr_pa Physical address of the L1 table.
 * @return The TTBR value.
 */
static inline uint32_t ttbr_value(uintptr_t ttbr_pa) { return (uint32_t)ttbr_pa | TTBR_WALK_ATTRS; }

uintptr_t ArchMmuCreateTables(AddressSpaceType type)
{
    const size_t l1_bytes = l1_table_bytes(type);
    const size_t l1_pages = l1_table_pages(type);

    uintptr_t l1_pa = PmmAllocFramesContigAligned(l1_pages, l1_pages);
    if (!l1_pa)
        return 0;

    assert((l1_pa & (l1_bytes - 1)) == 0);

    memset((void *)PA_TO_VA(l1_pa), 0, l1_bytes);

    return l1_pa;
}

void ArchMmuFreeTables(uintptr_t ttbr_pa, AddressSpaceType type)
{
    if (ttbr_pa == 0) {
        return;
    }

    uint32_t *l1 = (uint32_t *)PA_TO_VA(ttbr_pa);
    size_t entries = l1_entry_count(type);
    size_t pages = l1_table_pages(type);

    for (size_t i = 0; i < entries; i++) {
        if ((l1[i] & DESC_TYPE_MASK) == DESC_L2) {
            uint32_t l2_pa = l1[i] & L1_L2PTR_BASE_MASK;
            L2PtPoolFree(l2_pa);
        }
    }

    for (size_t i = 0; i < pages; i++) {
        PmmFreeFrame(ttbr_pa + i * PAGE_SIZE);
    }
}

/**
 * @brief Undoes part of a MapRange(): clears the sections and pages mapped in [@p va, @p va + @p size).
 *
 * @param[in,out] as   Address space.
 * @param[in]     va   Start of what was mapped.
 * @param[in]     size Bytes mapped so far.
 */
static void UnmapRangeMixed(AddressSpace *as, uintptr_t va, size_t size)
{
    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);

    for (uintptr_t off = 0; off < size;) {
        uintptr_t cur = va + off;
        uint32_t idx = L1_IDX(cur);

        if ((cur % SECTION_SIZE) == 0 && (l1[idx] & DESC_TYPE_MASK) == DESC_SECTION) {
            l1[idx] = 0;
            off += SECTION_SIZE;
        } else {
            ArchMmuUnmapPage(as, cur);
            off += PAGE_SIZE;
        }
    }
    Dsb();
}

/**
 * @brief Maps a page-aligned range, with sections wherever a whole empty section fits if allowed.
 *
 * @param[in,out] as             Address space.
 * @param[in]     va             Virtual start.
 * @param[in]     pa             Physical start.
 * @param[in]     size           Size in bytes.
 * @param[in]     prot           Protection.
 * @param[in]     memtype        Memory type.
 * @param[in]     allow_sections Whether sections may be used; user spaces never get them.
 * @return false if the range is misaligned or outside the space, or a table could not be
 *         allocated; nothing stays mapped then.
 */
static bool MapRange(AddressSpace *as, uintptr_t va, uintptr_t pa, size_t size, MemProt prot,
                     VirtMemType memtype, bool allow_sections)
{
    if (!as || size == 0)
        return false;
    if ((va | pa | size) % PAGE_SIZE)
        return false;
    if (as->type == ADDRESS_SPACE_USER && (va >= USER_VA_TOP || size > (USER_VA_TOP - va)))
        return false;

    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);
    size_t max_idx = l1_entry_count(as->type);
    bool sections = allow_sections && as->type != ADDRESS_SPACE_USER;

    for (uintptr_t off = 0; off < size;) {
        uintptr_t cur_va = va + off;
        uintptr_t cur_pa = pa + off;
        size_t idx = L1_IDX(cur_va);

        if (idx >= max_idx) {
            UnmapRangeMixed(as, va, off);
            return false;
        }

        if (sections && (cur_va % SECTION_SIZE) == 0 && (cur_pa % SECTION_SIZE) == 0 &&
            size - off >= SECTION_SIZE && (l1[idx] & DESC_TYPE_MASK) == DESC_FAULT) {
            l1[idx] = l1_section_desc(cur_pa, prot, memtype);
            off += SECTION_SIZE;
            continue;
        }

        if (!MmuMapPage(as, cur_va, cur_pa, memtype, prot)) {
            UnmapRangeMixed(as, va, off);
            return false;
        }
        off += PAGE_SIZE;
    }

    Dsb();
    return true;
}

bool ArchMmuMap(AddressSpace *as, uintptr_t va, uintptr_t pa, size_t size, MemProt prot,
                VirtMemType memtype)
{
    return MapRange(as, va, pa, size, prot, memtype, true);
}

bool ArchMmuMapPages(AddressSpace *as, uintptr_t va, uintptr_t pa, size_t size, MemProt prot,
                     VirtMemType memtype)
{
    return MapRange(as, va, pa, size, prot, memtype, false);
}

bool ArchMmuUnmap(AddressSpace *as, uintptr_t va, size_t size, bool flush)
{
    if (!as || size == 0) {
        return false;
    }

    bool unmapped_any = false;
    bool page_mode = false;
    size_t unmapped_pages = 0;
    bool needs_trailing_flush = false;

    if ((va % SECTION_SIZE) == 0 && (size % SECTION_SIZE) == 0) {
        uint32_t *l1_table = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);

        for (uintptr_t offset = 0; offset < size; offset += SECTION_SIZE) {
            size_t idx = L1_IDX(va + offset);

            uint32_t entry = l1_table[idx];
            if (entry == 0)
                continue;

            l1_table[idx] = 0;
            unmapped_any = true;

            if ((entry & DESC_TYPE_MASK) == DESC_L2) {
                if (flush) {
                    ArchSyncBarrier();
                    ArchMmuFlushTlbAsid(as->asid_token.asid);
                    ArchSyncBarrier();
                }
                L2PtPoolFree(entry & L1_L2PTR_BASE_MASK);
            } else {
                needs_trailing_flush = true;
            }
        }
    }
    else if ((va % PAGE_SIZE) == 0 && (size % PAGE_SIZE) == 0) {
        page_mode = true;
        for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE) {
            if (ArchMmuUnmapPage(as, va + offset)) {
                unmapped_any = true;
                unmapped_pages++;

                if (flush && size <= (UNMAP_TLBI_PAGE_THRESHOLD * PAGE_SIZE)) {
                    ArchMmuFlushTlbVaAsid(va + offset, as->asid_token.asid);
                }
            }
        }
    } else {
        return false;
    }

    if (unmapped_any && flush && (needs_trailing_flush || page_mode)) {
        if (!page_mode || size > (UNMAP_TLBI_PAGE_THRESHOLD * PAGE_SIZE) || unmapped_pages == 0)
            ArchMmuFlushTlbAsid(as->asid_token.asid);
        ArchSyncBarrier();
    }

    return unmapped_any;
}

bool ArchMmuProtect(AddressSpace *as, uintptr_t va, size_t size, MemProt prot)
{
    if (!as || size == 0)
        return false;

    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);
    bool changed = false;

    for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uintptr_t curr_va = va + offset;
        uint32_t l1_idx = L1_IDX(curr_va);
        uint32_t l1_entry = l1[l1_idx];
        uint32_t type = l1_entry & DESC_TYPE_MASK;

        if (type == DESC_SECTION) {
            l1[l1_idx] = l1_section_set_prot(l1_entry, prot);
            changed = true;
            offset += SECTION_SIZE - PAGE_SIZE;
            continue;
        }

        if (type == DESC_L2) {
            uint32_t l2_pa = l1_entry & L1_L2PTR_BASE_MASK;
            uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_pa);
            uint32_t l2_idx = L2_IDX(curr_va);

            if (!(l2[l2_idx] & L2_SMALL_TAG))
                continue;

            l2[l2_idx] = l2_page_set_prot(l2[l2_idx], prot);
            changed = true;
        }
    }

    if (changed) {
        ArchMmuFlushTlbAsid(as->asid_token.asid);
        ArchSyncBarrier();
    }
    return changed;
}

void ArchMmuEnable(AddressSpace *as)
{
    if (!as || as->pt_root_physaddr == 0) {
        return;
    }

    Dsb();
    Isb();

    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"(ttbr_value(as->pt_root_physaddr))
                     : "memory");

    __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" ::"r"(DACR_DOMAIN0_CLIENT) : "memory");

    ArchMmuFlushTlb();
    ArchSyncBarrier();

    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= MMU_BIT(SCTLR_M_BIT);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" ::"r"(sctlr) : "memory");

    ArchSyncBarrier();
}

void ArchMmuSwitch(AddressSpace *as)
{
    if (!as || as->pt_root_physaddr == 0) {
        return;
    }

    if (as->asid_token.generation != AsidCurrentGeneration()) {
        AsidFree(as->asid_token);
        as->asid_token = AsidAlloc();
    }

    __asm__ volatile("mcr p15, 0, %0, c13, c0, 1" ::"r"(0U) : "memory");

    Isb();

    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"(ttbr_value(as->pt_root_physaddr))
                     : "memory");

    Isb();

    __asm__ volatile("mcr p15, 0, %0, c13, c0, 1" ::"r"((uint32_t)as->asid_token.asid)
                     : "memory");

    /**
     * load bearing ISB. removed this and spent 12-14 sep 2026 entirely bugging
     * possibly the worst days of my life too
     * when everything was going wrong and i literally rewrote waitany
     * qemu won't show this because it doesn't really model the mmu truly
     */
    Isb();

    AsidSetActive(as->asid_token.asid);
}

void ArchMmuFlushTlb(void)
{
    uint32_t zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" ::"r"(zero) : "memory");
}

void ArchMmuFlushTlbAsid(uint8_t asid)
{
    if (asid == 0) {
        ArchMmuFlushTlb();
        return;
    }

    uint32_t asid_arg = (uint32_t)asid;
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 2" ::"r"(asid_arg) : "memory");
}

void ArchMmuFlushTlbVa(uintptr_t va)
{
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 3" ::"r"((uint32_t)(va & L2_SMALL_BASE_MASK))
                     : "memory");
}

void ArchMmuFlushTlbVaAsid(uintptr_t va, uint8_t asid)
{
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 1" ::"r"((uint32_t)((va & L2_SMALL_BASE_MASK) | asid))
                     : "memory");
}

uintptr_t ArchMmuTranslate(uintptr_t ttbr_pa, uintptr_t va)
{
    if (ttbr_pa == 0) {
        return 0;
    }

    uint32_t *l1_table = (uint32_t *)PA_TO_VA(ttbr_pa);
    uint32_t l1_entry = l1_table[L1_IDX(va)];
    uint32_t type = l1_entry & DESC_TYPE_MASK;

    if (type == DESC_FAULT) {
        return 0;
    }

    if (type == DESC_SECTION || type == DESC_SUPERSECTION) {
        uintptr_t section_base = (uintptr_t)(l1_entry & L1_SECTION_BASE_MASK);
        return section_base | (va & (SECTION_SIZE - 1));
    }

    if (type == DESC_L2) {
        uint32_t l2_table_pa = l1_entry & L1_L2PTR_BASE_MASK;
        uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_table_pa);
        uint32_t l2_entry = l2[L2_IDX(va)];

        if (!(l2_entry & L2_SMALL_TAG)) {
            return 0;
        }

        uint32_t page_pa = l2_entry & L2_SMALL_BASE_MASK;
        return page_pa | (va & (PAGE_SIZE - 1));
    }

    return 0;
}

/**
 * @brief Builds an L1 descriptor pointing to an L2 table.
 *
 * @param[in] l2_pa Physical address of the L2 table.
 * @return The descriptor, or 0 if @p l2_pa is 0.
 */
static uint32_t MmuMakeL1Pte(uintptr_t l2_pa)
{
    if (!l2_pa)
        return 0;
    return (uint32_t)(l2_pa & L1_L2PTR_BASE_MASK) | L1_L2PTR_TAG;
}

/**
 * @brief Replaces a section with an L2 table mapping the same 1 MiB with the same attributes.
 *
 * @param[in,out] l1     L1 table.
 * @param[in]     l1_idx Index of the section.
 * @param[in]     asid   ASID whose TLB entries are flushed afterwards.
 * @return false if no L2 table could be allocated.
 */
static bool MmuBreakSection(uint32_t *l1, uint32_t l1_idx, uint8_t asid)
{
    uint32_t section = l1[l1_idx];
    uintptr_t section_pa = section & L1_SECTION_BASE_MASK;

    uint32_t xn = (section >> L1_SECT_XN_BIT) & 0x1U;
    uint32_t cb = (section >> L1_SECT_B_BIT) & CB_MASK;
    uint32_t ap = (section >> L1_SECT_AP_SHIFT) & AP_MASK;
    uint32_t tex = (section >> L1_SECT_TEX_SHIFT) & TEX_MASK;
    uint32_t ap2 = (section >> L1_SECT_AP2_BIT) & 0x1U;
    uint32_t ng = (section >> L1_SECT_NG_BIT) & 0x1U;
    uint32_t s = (section >> L1_SECT_S_BIT) & 0x1U;

    uintptr_t l2_pa = (uintptr_t)L2PtPoolAlloc();
    if (!l2_pa)
        return false;

    uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_pa);

    const uint32_t attrs = (xn << L2_PAGE_XN_BIT) | (cb << L2_PAGE_B_BIT) |
                           (ap << L2_PAGE_AP_SHIFT) | (tex << L2_PAGE_TEX_SHIFT) |
                           (s << L2_PAGE_S_BIT) | (ap2 << L2_PAGE_AP2_BIT) | (ng << L2_PAGE_NG_BIT);

    for (uint32_t i = 0; i < L2_ENTRIES; i++) {
        uintptr_t page_pa = section_pa + (uintptr_t)i * PAGE_SIZE;
        l2[i] = (uint32_t)(page_pa & L2_SMALL_BASE_MASK) | L2_SMALL_TAG | attrs;
    }

    l1[l1_idx] = MmuMakeL1Pte(l2_pa);

    ArchMmuFlushTlbAsid(asid);
    ArchSyncBarrier();

    return true;
}

static bool MmuMapPage(AddressSpace *as, uintptr_t va, uintptr_t pa, VirtMemType memtype,
                           MemProt prot)
{
    if (!as) {
        return false;
    }

    if (as->type == ADDRESS_SPACE_USER && va >= USER_VA_TOP) {
        return false;
    }

    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);

    uint32_t l1_idx = L1_IDX(va);
    uint32_t l2_idx = L2_IDX(va);
    if (l1_idx >= l1_entry_count(as->type))
        return false;

    uint32_t l1_entry = l1[l1_idx];
    uint32_t type = l1_entry & DESC_TYPE_MASK;

    uint32_t *l2;

    if (type == DESC_FAULT) {
        uintptr_t l2_pa = (uintptr_t)L2PtPoolAlloc();
        if (!l2_pa)
            return false;

        l1[l1_idx] = MmuMakeL1Pte(l2_pa);
        l2 = (uint32_t *)PA_TO_VA(l2_pa);
    } else if (type == DESC_L2) {
        l2 = (uint32_t *)PA_TO_VA(l1_entry & L1_L2PTR_BASE_MASK);
    } else {
        if (!MmuBreakSection(l1, l1_idx, as->asid_token.asid))
            return false;

        l2 = (uint32_t *)PA_TO_VA(l1[l1_idx] & L1_L2PTR_BASE_MASK);
    }

    l2[l2_idx] = l2_page_desc(pa, prot, memtype);

    Dsb();
    return true;
}

bool ArchMmuUnmapPage(AddressSpace *as, uintptr_t va)
{
    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);

    uint32_t l1_idx = L1_IDX(va);
    uint32_t l2_idx = L2_IDX(va);

    uint32_t l1_entry = l1[l1_idx];

    if ((l1_entry & DESC_TYPE_MASK) != DESC_L2) {
        return false;
    }

    uint32_t *l2 = (uint32_t *)PA_TO_VA(l1_entry & L1_L2PTR_BASE_MASK);

    if (l2[l2_idx] == 0) {
        return false;
    }
    l2[l2_idx] = 0;
    return true;
}

/**
 * @brief Finds what backs the region containing an address.
 *
 * @param[in] as Address space.
 * @param[in] va Virtual address.
 * @return The region's backing, or VM_BACKING_ANON if no region contains @p va.
 */
static VirtMemBacking mmu_region_owner_for_va(const AddressSpace *as, uintptr_t va)
{
    if (!as)
        return VM_BACKING_ANON;

    for (uint32_t i = 0; i < as->regions.len; i++) {
        const VirtMemRegion *r = vm_region_vec_get_const(&as->regions, i);
        if (!r)
            continue;

        uintptr_t start = r->vaddr_start;
        uintptr_t end = start + r->size;
        if (end < start)
            continue;

        if (va >= start && va < end)
            return r->owner;
    }

    return VM_BACKING_ANON;
}

void ArchMmuFreeUserPages(AddressSpace *as)
{
    if (!as)
        return;

    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);

    size_t entries = l1_entry_count(ADDRESS_SPACE_USER);
    for (size_t i = 0; i < entries; i++) {
        uint32_t l1_entry = l1[i];

        if ((l1_entry & DESC_TYPE_MASK) != DESC_L2)
            continue;

        uint32_t *l2 = (uint32_t *)PA_TO_VA(l1_entry & L1_L2PTR_BASE_MASK);

        for (size_t j = 0; j < L2_ENTRIES; j++) {
            if (!(l2[j] & L2_SMALL_TAG))
                continue;

            uintptr_t va = ((uintptr_t)i << MMU_SECTION_SHIFT) | ((uintptr_t)j << MMU_PAGE_SHIFT);

            if (va == USER_SYSPAGE_VA)
                continue;

            if (mmu_region_owner_for_va(as, va) != VM_BACKING_ANON)
                continue;

            if ((l2[j] & L2_PAGE_ATTR_MASK) == L2_PAGE_ATTR_DEVICE)
                continue;

            PmmFreeFrame(l2[j] & L2_SMALL_BASE_MASK);
        }
    }
}

void ArchMmuInitTtbr1(AddressSpace *as)
{
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 1" ::"r"(ttbr_value(as->pt_root_physaddr))
                     : "memory");

    Isb();

    uint32_t ttbcr;
    __asm__ volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr)::"memory");
    ttbcr &= ~(TTBCR_N_MASK | MMU_BIT(3) | MMU_BIT(TTBCR_PD0_BIT));
    ttbcr |= TTBCR_N_SPLIT_2GB;
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 2" ::"r"(ttbcr) : "memory");

    ArchMmuFlushTlb();
    ArchSyncBarrier();
}

/**
 * @brief The RAM range ArchMmuInstallKernelSpace() identity-maps, widened to whole sections.
 *
 * @param[out] start Physical start, section-aligned.
 * @param[out] size  Size in bytes, a whole number of sections.
 */
static void IdentityRange(uintptr_t *start, size_t *size)
{
    uintptr_t lo = kernel_layout.ram_start & ~(uintptr_t)(SECTION_SIZE - 1);
    uintptr_t hi = (kernel_layout.ram_end + SECTION_SIZE - 1) & ~(uintptr_t)(SECTION_SIZE - 1);

    *start = lo;
    *size = hi - lo;
}

bool ArchMmuInstallKernelSpace(AddressSpace *as)
{
    uintptr_t id_start;
    size_t id_size;

    IdentityRange(&id_start, &id_size);
    if (!ArchMmuMap(as, id_start, id_start, id_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                    VM_MEM_NORMAL))
        return false;

    if (!ArchMmuMap(as, BOARD_PERIPH_PA_BASE, BOARD_PERIPH_PA_BASE,
                    (size_t)BOARD_PERIPH_MB * SECTION_SIZE, PROT_READ | PROT_WRITE, VM_MEM_DEVICE))
        return false;

    Dsb();
    __asm__ volatile("mcr p15, 0, %0, c13, c0, 1" ::"r"(0U) : "memory");
    Isb();
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"(ttbr_value(as->pt_root_physaddr))
                     : "memory");
    Isb();
    AsidSetActive(0);

    ArchMmuFlushTlb();
    ArchSyncBarrier();
    return true;
}

void ArchMmuDropIdentity(AddressSpace *as)
{
    uintptr_t id_start;
    size_t id_size;

    if (ArchCurrentStackPointer() < KERNEL_VA_BASE)
        RelocateStacks(KERNEL_VA_OFFSET);

    IdentityRange(&id_start, &id_size);
    ArchMmuUnmap(as, id_start, id_size, true);
}
