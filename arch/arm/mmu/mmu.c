// mmu.c - ARM MMU implementation (ARMv7-A short-descriptor, 2-level)
//
// Descriptor layouts, field positions and attribute encodings live in
// armv7_mmu.h; this file is the logic that builds and edits them.

#include "kernel/mm/pmm.h"
#include "l2_pool.h"
#include "zuzu/types.h"
#include <arch/asid.h>
#include <arch/barrier.h>
#include <arch/mmu.h>
#include <arch_impl/armv7_mmu.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

#define LOG_FMT(fmt) "(mmu) " fmt
#include <zuzu/log.h>

extern uint8_t dirty_bitmap[];

/* Above this many pages, a single by-ASID flush is cheaper than a per-page
 * TLBI loop. Below it, the narrower by-VA invalidation keeps the rest of the
 * address space's translations warm. */
#define UNMAP_TLBI_PAGE_THRESHOLD 16U

static bool arch_mmu_map_page(AddressSpace *as, uintptr_t va, uintptr_t pa, VirtMemType memtype,
                              MemProt prot);

// ---- Address-space geometry ----------------------------------------------

static inline size_t l1_entry_count(AsType type)
{
    return (type == ADDRSPACE_USER) ? L1_ENTRIES_USER : L1_ENTRIES_KERNEL;
}

static inline size_t l1_table_bytes(AsType type) { return l1_entry_count(type) * L1_DESC_BYTES; }

static inline size_t l1_table_pages(AsType type) { return l1_table_bytes(type) / PAGE_SIZE; }

// ---- Descriptor attribute encoding ---------------------------------------

// AP[1:0] field value. AP[2] stays 0 for every mapping zuzu creates.
static inline uint32_t ap_bits(MemProt prot)
{
    if (prot & VM_PROT_USER)
        return (prot & PROT_WRITE) ? AP_USER_RW : AP_USER_RO;
    return AP_KERNEL_RW;
}

// Build a 1 MB section descriptor.
static uint32_t l1_section_desc(uintptr_t pa, MemProt prot, VirtMemType memtype)
{
    uint32_t e = (uint32_t)(pa & L1_SECTION_BASE_MASK) | L1_SECTION_TAG;

    e |= ap_bits(prot) << L1_SECT_AP_SHIFT;
    if (!(prot & PROT_EXEC))
        e |= MMU_BIT(L1_SECT_XN_BIT);
    if (prot & VM_PROT_USER)
        e |= MMU_BIT(L1_SECT_NG_BIT);
    if (memtype == VM_MEM_DEVICE)
        e |= L1_SECT_ATTR_DEVICE;
    else
        e |= L1_SECT_ATTR_NORMAL | MMU_BIT(L1_SECT_S_BIT);

    return e;
}

// Build a 4 KB small-page descriptor.
static uint32_t l2_page_desc(uintptr_t pa, MemProt prot, VirtMemType memtype)
{
    uint32_t e = (uint32_t)(pa & L2_SMALL_BASE_MASK) | L2_SMALL_TAG;

    e |= ap_bits(prot) << L2_PAGE_AP_SHIFT;
    if (!(prot & PROT_EXEC))
        e |= MMU_BIT(L2_PAGE_XN_BIT);
    if (prot & VM_PROT_USER)
        e |= MMU_BIT(L2_PAGE_NG_BIT); // ASID-tagged, not visible across address spaces
    if (memtype == VM_MEM_DEVICE) 
        e |= L2_PAGE_ATTR_DEVICE;
    else
        e |= L2_PAGE_ATTR_NORMAL | MMU_BIT(L2_PAGE_S_BIT); // <-- add

    return e;
}

// Rewrite only the permission bits of an existing section/page descriptor,
// preserving its physical base and memory-type attributes.
static uint32_t l1_section_set_prot(uint32_t e, MemProt prot)
{
    e &= ~((AP_MASK << L1_SECT_AP_SHIFT) | MMU_BIT(L1_SECT_AP2_BIT) | MMU_BIT(L1_SECT_XN_BIT));
    e |= ap_bits(prot) << L1_SECT_AP_SHIFT;
    if (!(prot & PROT_EXEC))
        e |= MMU_BIT(L1_SECT_XN_BIT);
    return e;
}

static uint32_t l2_page_set_prot(uint32_t e, MemProt prot)
{
    e &= ~((AP_MASK << L2_PAGE_AP_SHIFT) | MMU_BIT(L2_PAGE_AP2_BIT) | MMU_BIT(L2_PAGE_XN_BIT));
    e |= ap_bits(prot) << L2_PAGE_AP_SHIFT;
    if (!(prot & PROT_EXEC))
        e |= MMU_BIT(L2_PAGE_XN_BIT);
    return e;
}

// TTBR value for a translation-table base PA. See TTBR_WALK_ATTRS in
// armv7_mmu.h for why the walk attributes must match the table's own mapping.
static inline uint32_t ttbr_value(uintptr_t ttbr_pa) { return (uint32_t)ttbr_pa | TTBR_WALK_ATTRS; }

uintptr_t arch_mmu_create_tables(AsType type)
{
    const size_t l1_bytes = l1_table_bytes(type);
    const size_t l1_pages = l1_table_pages(type);

    // Alignment must equal table size for both user (8 KB) and kernel (16 KB).
    uintptr_t l1_pa = PmmAllocFramesContigAligned(l1_pages, l1_pages);
    if (!l1_pa)
        return 0;

    assert((l1_pa & (l1_bytes - 1)) == 0);

    memset((void *)PA_TO_VA(l1_pa), 0, l1_bytes);

    return l1_pa;
}

void arch_mmu_free_tables(uintptr_t ttbr_pa, AsType type)
{
    if (ttbr_pa == 0)
    {
        return;
    }

    // Free every L2 table referenced by the L1, then the L1 pages themselves.
    // The L1 occupies l1_table_pages() contiguous PMM pages.
    uint32_t *l1 = (uint32_t *)PA_TO_VA(ttbr_pa);
    size_t entries = l1_entry_count(type);
    size_t pages = l1_table_pages(type);

    for (size_t i = 0; i < entries; i++)
    {
        if ((l1[i] & DESC_TYPE_MASK) == DESC_L2)
        {
            uint32_t l2_pa = l1[i] & L1_L2PTR_BASE_MASK;
            l2_pool_free(l2_pa);
        }
    }

    for (size_t i = 0; i < pages; i++)
    {
        PmmFreeFrame(ttbr_pa + i * PAGE_SIZE);
    }
}

bool arch_mmu_map(AddressSpace *as, uintptr_t va, uintptr_t pa, size_t size, MemProt prot,
                  VirtMemType memtype)
{
    if (!as || size == 0)
    {
        return false;
    }

    // TTBR0 user tables only cover [0, USER_VA_TOP) when N=1.
    // Reject ranges that would index beyond the user table.
    if (as->type == ADDRSPACE_USER)
    {
        if (va >= USER_VA_TOP || size > (USER_VA_TOP - va))
        {
            return false;
        }
    }

    if ((va % SECTION_SIZE) == 0 && (pa % SECTION_SIZE) == 0 && (size % SECTION_SIZE) == 0)
    {
        // During identity-map bring-up, TTBR0 PA is directly addressable (MMU off / identity).
        uint32_t *l1_table = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);
        size_t max_idx = l1_entry_count(as->type);

        for (uintptr_t offset = 0; offset < size; offset += SECTION_SIZE)
        {
            uintptr_t curr_va = va + offset;
            uintptr_t curr_pa = pa + offset;

            size_t idx = L1_IDX(curr_va);
            if (idx >= max_idx)
                return false;

            l1_table[idx] = l1_section_desc(curr_pa, prot, memtype);
        }

        // The descriptors must be observable to the table walker before the
        // caller uses the mapping -- IoRemap() hands the VA straight to a
        // driver that performs MMIO on it.
        ArchDsb();
        return true;
    }

    if ((va % PAGE_SIZE == 0) && (pa % PAGE_SIZE == 0) && (size % PAGE_SIZE == 0))
    {
        for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE)
        {
            if (!arch_mmu_map_page(as, va + offset, pa + offset, memtype, prot))
            {
                // Unmap everything that was mapped so far
                for (uintptr_t rollback = 0; rollback < offset; rollback += PAGE_SIZE)
                {
                    arch_mmu_unmap_page(as, va + rollback);
                }
                return false;
            }
        }

        return true;
    }

    return false;
}

bool arch_mmu_unmap(AddressSpace *as, uintptr_t va, size_t size, bool flush)
{
    if (!as || size == 0)
    {
        return false;
    }

    bool unmapped_any = false;
    bool page_mode = false;
    size_t unmapped_pages = 0;
    bool needs_trailing_flush = false;

    // Section-aligned: use sections
    if ((va % SECTION_SIZE) == 0 && (size % SECTION_SIZE) == 0)
    {
        uint32_t *l1_table = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);


        for (uintptr_t offset = 0; offset < size; offset += SECTION_SIZE)
        {
            size_t idx = L1_IDX(va + offset);

            uint32_t entry = l1_table[idx];
            if (entry == 0)
                continue;

            /* Break-before-make: invalidate the L1 descriptor and make the
             * change visible to the walker BEFORE releasing the L2 table it
             * pointed to, so no concurrent or speculative walk can read a
             * freed (and possibly reallocated) table. */
            l1_table[idx] = 0;
            unmapped_any = true;

            if ((entry & DESC_TYPE_MASK) == DESC_L2)
            {
                if (flush) {
                    ArchCtxSync();                                /* DSB: zero visible */
                    arch_mmu_flush_tlb_asid(as->asid_token.asid); /* drop cached walks */
                    ArchCtxSync();
                }
                l2_pool_free(entry & L1_L2PTR_BASE_MASK); /* now safe to free */
            } else {
                needs_trailing_flush = true;
            }
        }
    }
    // Page-aligned: use pages
    else if ((va % PAGE_SIZE) == 0 && (size % PAGE_SIZE) == 0)
    {
        page_mode = true;
        for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE)
        {
            if (arch_mmu_unmap_page(as, va + offset))
            {
                unmapped_any = true;
                unmapped_pages++;

                // For small unmaps, invalidate only the touched virtual address.
                if (flush && size <= (UNMAP_TLBI_PAGE_THRESHOLD * PAGE_SIZE))
                {
                    arch_mmu_flush_tlb_va_asid(va + offset, as->asid_token.asid);
                }
            }
        }
    }
    else
    {
        return false;
    }

    if (unmapped_any && flush && (needs_trailing_flush || page_mode))
    {
        if (!page_mode || size > (UNMAP_TLBI_PAGE_THRESHOLD * PAGE_SIZE) || unmapped_pages == 0)
            arch_mmu_flush_tlb_asid(as->asid_token.asid);
        ArchCtxSync();
    }

    return unmapped_any;
}

bool arch_mmu_protect(AddressSpace *as, uintptr_t va, size_t size, MemProt prot)
{
    if (!as || size == 0)
        return false;

    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);
    bool changed = false;

    for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE)
    {
        uintptr_t curr_va = va + offset;
        uint32_t l1_idx = L1_IDX(curr_va);
        uint32_t l1_entry = l1[l1_idx];
        uint32_t type = l1_entry & DESC_TYPE_MASK;

        if (type == DESC_SECTION)
        {
            // Section entry, rewrite permission bits in place.
            l1[l1_idx] = l1_section_set_prot(l1_entry, prot);
            changed = true;
            // Skip to next section boundary
            offset += SECTION_SIZE - PAGE_SIZE;
            continue;
        }

        if (type == DESC_L2)
        {
            // Page table, rewrite L2 entry
            uint32_t l2_pa = l1_entry & L1_L2PTR_BASE_MASK;
            uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_pa);
            uint32_t l2_idx = L2_IDX(curr_va);

            if (!(l2[l2_idx] & L2_SMALL_TAG))
                continue; // not mapped

            l2[l2_idx] = l2_page_set_prot(l2[l2_idx], prot);
            changed = true;
        }
    }

    if (changed)
    {
        arch_mmu_flush_tlb_asid(as->asid_token.asid);
        ArchCtxSync();
    }
    return changed;
}

void arch_mmu_enable(AddressSpace *as)
{
    if (!as || as->pt_root_physaddr == 0)
    {
        return;
    }

    // Barriers before changing translation context.
    ArchDsb();
    ArchIsb();

    // TTBR0 = L1 table base (cacheable walks).
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"(ttbr_value(as->pt_root_physaddr))
                     : "memory");

    // Domain Access Control: domain 0 = Client, so descriptor permissions apply.
    __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" ::"r"(DACR_DOMAIN0_CLIENT) : "memory");

    // Invalidate TLB before enabling.
    arch_mmu_flush_tlb();
    ArchCtxSync();

    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= MMU_BIT(SCTLR_M_BIT);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" ::"r"(sctlr) : "memory");

    // Synchronize after enabling MMU.
    ArchCtxSync();
}

void arch_mmu_switch(AddressSpace *as)
{
    if (!as || as->pt_root_physaddr == 0)
    {
        return;
    }

    if (as->asid_token.generation != asid_current_generation())
    {
        asid_free(as->asid_token); // free the old ASID (no-op if already reclaimed)
        as->asid_token = asid_alloc();
    }

    /* Park on reserved ASID 0: no speculative walk during the TTBR0 change
     * can then allocate a TLB entry tagged with a live ASID. */
    __asm__ volatile("mcr p15, 0, %0, c13, c0, 1" ::"r"(0U) : "memory"); // CONTEXTIDR
    __asm__ volatile("isb" ::: "memory");

    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"(ttbr_value(as->pt_root_physaddr))
                     : "memory"); // TTBR0
    __asm__ volatile("isb" ::: "memory");

    __asm__ volatile("mcr p15, 0, %0, c13, c0, 1" ::"r"((uint32_t)as->asid_token.asid)
                     : "memory"); // CONTEXTIDR

    // Tell asid_alloc() which ASID is now actually live in hardware, so a
    // rollover reserves it instead of handing it to a second address space.
    AsidSetActive(as->asid_token.asid);
}

void arch_mmu_flush_tlb(void)
{
    // TLBIALL: invalidate the entire unified TLB.
    uint32_t zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" ::"r"(zero) : "memory");
}

void arch_mmu_flush_tlb_asid(uint8_t asid)
{
    if (asid == 0)
    {
        arch_mmu_flush_tlb();
        return;
    }

    // TLBIASID: invalidate every entry tagged with this ASID.
    uint32_t asid_arg = (uint32_t)asid;
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 2" ::"r"(asid_arg) : "memory");
}

// TLBIMVAA: by MVA, all ASIDs. For kernel/global VAs.
void arch_mmu_flush_tlb_va(uintptr_t va)
{
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 3" ::"r"((uint32_t)(va & L2_SMALL_BASE_MASK))
                     : "memory");
}

// TLBIMVA: by MVA + ASID. For user VAs, whose entries are nG and therefore
// ASID-tagged -- TLBIMVAA would also evict every other address space's
// translation for the same VA.
void arch_mmu_flush_tlb_va_asid(uintptr_t va, uint8_t asid)
{
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 1" ::"r"((uint32_t)((va & L2_SMALL_BASE_MASK) | asid))
                     : "memory");
}

uintptr_t arch_mmu_translate(PhysAddr ttbr_pa, VirtAddr va)
{
    if (ttbr_pa == 0)
    {
        return 0;
    }

    uint32_t *l1_table = (uint32_t *)PA_TO_VA(ttbr_pa);
    uint32_t l1_entry = l1_table[L1_IDX(va)];
    uint32_t type = l1_entry & DESC_TYPE_MASK;

    if (type == DESC_FAULT)
    {
        return 0; // unmapped
    }

    if (type == DESC_SECTION || type == DESC_SUPERSECTION)
    {
        // Supersections are treated as sections here; zuzu never creates one.
        uintptr_t section_base = (uintptr_t)(l1_entry & L1_SECTION_BASE_MASK);
        return section_base | (va & (SECTION_SIZE - 1));
    }

    if (type == DESC_L2)
    {
        uint32_t l2_table_pa = l1_entry & L1_L2PTR_BASE_MASK;
        uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_table_pa);
        uint32_t l2_entry = l2[L2_IDX(va)];

        if (!(l2_entry & L2_SMALL_TAG))
        {
            return 0; // not a valid small page
        }

        uint32_t page_pa = l2_entry & L2_SMALL_BASE_MASK;
        return page_pa | (va & (PAGE_SIZE - 1));
    }

    return 0;
}

static uintptr_t arch_mmu_alloc_l2_table(void) { return l2_pool_alloc(); }

static uint32_t arch_mmu_make_l1_pte(uintptr_t l2_pa)
{
    if (!l2_pa)
        return 0;
    return (uint32_t)(l2_pa & L1_L2PTR_BASE_MASK) | L1_L2PTR_TAG;
}

/*
 * Break a 1MB section mapping into 256 equivalent 4KB page mappings.
 * Allocates an L2 table, transcodes the section's physical base and attributes
 * into small-page descriptors, then replaces the L1 section entry with an
 * L1 page-table pointer. After this, individual pages can be remapped or
 * unmapped within the former section.
 */
static bool arch_mmu_break_section(uint32_t *l1, uint32_t l1_idx, uint8_t asid)
{
    uint32_t section = l1[l1_idx];
    uintptr_t section_pa = section & L1_SECTION_BASE_MASK;

    /* The two descriptor formats carry the same fields at different offsets,
     * so every attribute has to be shifted from its section position to its
     * small-page position. */
    uint32_t xn = (section >> L1_SECT_XN_BIT) & 0x1U;
    uint32_t cb = (section >> L1_SECT_B_BIT) & CB_MASK;
    uint32_t ap = (section >> L1_SECT_AP_SHIFT) & AP_MASK;
    uint32_t tex = (section >> L1_SECT_TEX_SHIFT) & TEX_MASK;
    uint32_t ap2 = (section >> L1_SECT_AP2_BIT) & 0x1U;
    uint32_t ng = (section >> L1_SECT_NG_BIT) & 0x1U;

    uintptr_t l2_pa = arch_mmu_alloc_l2_table();
    if (!l2_pa)
        return false;

    uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_pa);

    const uint32_t attrs = (xn << L2_PAGE_XN_BIT) | (cb << L2_PAGE_B_BIT) |
                           (ap << L2_PAGE_AP_SHIFT) | (tex << L2_PAGE_TEX_SHIFT) |
                           (ap2 << L2_PAGE_AP2_BIT) | (ng << L2_PAGE_NG_BIT);

    /* Replicate the section mapping at 4 KB granularity */
    for (uint32_t i = 0; i < L2_ENTRIES; i++)
    {
        uintptr_t page_pa = section_pa + (uintptr_t)i * PAGE_SIZE;
        l2[i] = (uint32_t)(page_pa & L2_SMALL_BASE_MASK) | L2_SMALL_TAG | attrs;
    }

    /* Replace the section entry with an L1 page-table descriptor */
    l1[l1_idx] = arch_mmu_make_l1_pte(l2_pa);

    /* The old section's TLB entries are now stale */
    arch_mmu_flush_tlb_asid(asid);
    ArchCtxSync();

    return true;
}

static bool arch_mmu_map_page(AddressSpace *as, uintptr_t va, uintptr_t pa, VirtMemType memtype,
                              MemProt prot)
{
    if (!as)
    {
        return false;
    }

    // Single-page user mappings must stay within the TTBR0 user range.
    if (as->type == ADDRSPACE_USER && va >= USER_VA_TOP)
    {
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

    if (type == DESC_FAULT)
    {
        // Unmapped - allocate a fresh L2 table
        uintptr_t l2_pa = arch_mmu_alloc_l2_table();
        if (!l2_pa)
            return false;

        l1[l1_idx] = arch_mmu_make_l1_pte(l2_pa);
        l2 = (uint32_t *)PA_TO_VA(l2_pa);
    }
    else if (type == DESC_L2)
    {
        // Already an L2 page table - reuse it
        l2 = (uint32_t *)PA_TO_VA(l1_entry & L1_L2PTR_BASE_MASK);
    }
    else
    {
        // Section mapping - break it into page entries first
        if (!arch_mmu_break_section(l1, l1_idx, as->asid_token.asid))
            return false;

        l2 = (uint32_t *)PA_TO_VA(l1[l1_idx] & L1_L2PTR_BASE_MASK);
    }

    // Now install the page entry
    l2[l2_idx] = l2_page_desc(pa, prot, memtype);

    ArchDsb();
    return true;
}

bool arch_mmu_unmap_page(AddressSpace *as, uintptr_t va)
{
    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);

    uint32_t l1_idx = L1_IDX(va);
    uint32_t l2_idx = L2_IDX(va);

    uint32_t l1_entry = l1[l1_idx];

    if ((l1_entry & DESC_TYPE_MASK) != DESC_L2)
    {
        return false; // Not a page table, can't unmap page
    }

    uint32_t *l2 = (uint32_t *)PA_TO_VA(l1_entry & L1_L2PTR_BASE_MASK);

    if (l2[l2_idx] == 0)
    {
        return false; // wasn't mapped
    }
    l2[l2_idx] = 0;
    return true;
}

static VirtMemOwner mmu_region_owner_for_va(const AddressSpace *as, uintptr_t va)
{
    if (!as)
        return VM_OWNER_ANON;

    for (uint32_t i = 0; i < as->regions.len; i++)
    {
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

    // If region metadata is missing, keep old behavior and reclaim.
    return VM_OWNER_ANON;
}

void arch_mmu_free_user_pages(AddressSpace *as)
{
    if (!as)
        return;

    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->pt_root_physaddr);

    // Walk the user range of the L1. Only free the BACKING physical pages,
    // not the page-table structures: L2 tables and L1 pages are freed
    // separately by arch_mmu_free_tables().
    size_t entries = l1_entry_count(ADDRSPACE_USER);
    for (size_t i = 0; i < entries; i++)
    {
        uint32_t l1_entry = l1[i];

        // Section mappings are not used for user space currently.
        if ((l1_entry & DESC_TYPE_MASK) != DESC_L2)
            continue;

        uint32_t *l2 = (uint32_t *)PA_TO_VA(l1_entry & L1_L2PTR_BASE_MASK);

        for (size_t j = 0; j < L2_ENTRIES; j++)
        {
            if (!(l2[j] & L2_SMALL_TAG))
                continue; // not a valid small page

            uintptr_t va = ((uintptr_t)i << MMU_SECTION_SHIFT) | ((uintptr_t)j << MMU_PAGE_SHIFT);

            // Keep the shared kernel-exported syspage mapped read-only.
            if (va == USER_SYSPAGE_VA)
                continue;

            // Only reclaim pages owned by this address space.
            if (mmu_region_owner_for_va(as, va) != VM_OWNER_ANON)
                continue;

            // Device mappings are not PMM-owned pages.
            if ((l2[j] & L2_PAGE_ATTR_MASK) == L2_PAGE_ATTR_DEVICE)
                continue;

            PmmFreeFrame(l2[j] & L2_SMALL_BASE_MASK);
        }
    }
}

void arch_mmu_init_ttbr1(AddressSpace *as)
{
    // Mirror the kernel L1 into TTBR1, then set TTBCR.N to split at USER_VA_TOP.
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 1" ::"r"(ttbr_value(as->pt_root_physaddr))
                     : "memory");

    ArchIsb();

    uint32_t ttbcr;
    __asm__ volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr)::"memory");
    // Clear N[2:0], the SBZ bit3, and PD0. PD1 and everything above are left as found.
    ttbcr &= ~(TTBCR_N_MASK | MMU_BIT(3) | MMU_BIT(TTBCR_PD0_BIT));
    ttbcr |= TTBCR_N_SPLIT_2GB;
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 2" ::"r"(ttbcr) : "memory");

    arch_mmu_flush_tlb();
    ArchCtxSync();
}
