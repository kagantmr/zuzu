// mmu.c - ARM MMU implementation (ARMv7-A short-descriptor, 2-level)

#include <arch/mmu.h>
#include "kernel/mm/pmm.h"
#include "l2_pool.h"
#include <stdint.h>
#include <mem.h>
#include <assert.h>

#define LOG_FMT(fmt) "(mmu) " fmt
#include "core/log.h"

// L1 descriptor type bits[1:0]
#define DESC_FAULT     0x0u   // unmapped
#define DESC_L2        0x1u   // pointer to an L2 page table
#define DESC_SECTION   0x2u   // 1 MB section
#define DESC_TYPE_MASK 0x3u

// Descriptor type tags written into table entries.
#define L1_SECTION_TAG DESC_SECTION  // bits[1:0]=0b10 for a section entry
#define L1_L2PTR_TAG   DESC_L2       // bits[1:0]=0b01 for an L1->L2 pointer
#define L2_SMALL_TAG   0x2u          // bit1=1 for a small (4 KB) page entry

#define ALIGNMENT_1MB_MASK 0xFFF00000u
#define ALIGNMENT_1KB_MASK 0xFFFFFC00u
#define ALIGNMENT_4KB_MASK 0xFFFFF000u

#define L2_ENTRIES 256u  // small-page entries per L2 table

// Page-table index extraction from a virtual address.
#define L1_IDX(va) (((va) >> 20) & 0xFFFu)  // bits[31:20]
#define L2_IDX(va) (((va) >> 12) & 0xFFu)   // bits[19:12]

static bool arch_mmu_map_page(addrspace_t *as, uintptr_t va, uintptr_t pa,
                              vm_memtype_t memtype, vm_prot_t prot);

// ---- Address-space geometry ----------------------------------------------
// With TTBCR.N=1 the user L1 covers [0, USER_VA_TOP) with 2048 entries (8 KB);
// the kernel L1 is the full 4096 entries (16 KB).

static inline size_t l1_entry_count(addrspace_type_t type)
{
    return (type == ADDRSPACE_USER) ? 2048u : 4096u;
}

static inline size_t l1_table_bytes(addrspace_type_t type)
{
    return (type == ADDRSPACE_USER) ? (8u * 1024u) : (16u * 1024u);
}

static inline size_t l1_table_pages(addrspace_type_t type)
{
    return l1_table_bytes(type) / PAGE_SIZE;
}

// ---- Descriptor attribute encoding ---------------------------------------
// AP[1:0] field value. AP[2] stays 0 for every mapping zuzu creates.
//   0b01 kernel RW / no user, 0b10 user RO, 0b11 user RW.
static inline uint32_t ap_bits(vm_prot_t prot)
{
    if (prot & VM_PROT_USER)
        return (prot & VM_PROT_WRITE) ? 0x3u : 0x2u;
    return 0x1u;
}

// Build a 1 MB section descriptor.
//   AP[1:0] -> bits[11:10], XN -> bit4, memtype -> TEX[14:12]/C[3]/B[2].
static uint32_t l1_section_desc(uintptr_t pa, vm_prot_t prot, vm_memtype_t memtype)
{
    uint32_t e = (uint32_t)(pa & ALIGNMENT_1MB_MASK) | L1_SECTION_TAG;
    e |= ap_bits(prot) << 10;
    if (!(prot & VM_PROT_EXEC))
        e |= (1u << 4); // XN
    if (memtype == VM_MEM_DEVICE)
        e |= (1u << 2); // Device: TEX=0, C=0, B=1
    else
        e |= (1u << 12) | (1u << 3) | (1u << 2); // Normal WB-WA: TEX=001, C=1, B=1
    return e;
}

// Build a 4 KB small-page descriptor.
//   AP[1:0] -> bits[5:4], XN -> bit0, memtype -> TEX[8:6]/C[3]/B[2].
static uint32_t l2_page_desc(uintptr_t pa, vm_prot_t prot, vm_memtype_t memtype)
{
    uint32_t e = (uint32_t)(pa & ALIGNMENT_4KB_MASK) | L2_SMALL_TAG;
    if (!(prot & VM_PROT_EXEC))
        e |= 0x1u; // XN
    e |= ap_bits(prot) << 4;
    if (memtype == VM_MEM_DEVICE)
        e |= (1u << 2); // Device: TEX=0, C=0, B=1
    else
        e |= (1u << 6) | (1u << 3) | (1u << 2); // Normal WB-WA: TEX=001, C=1, B=1
    return e;
}

// Rewrite only the permission bits of an existing section/page descriptor,
// preserving its physical base and memory-type attributes.
static uint32_t l1_section_set_prot(uint32_t e, vm_prot_t prot)
{
    e &= ~((0x3u << 10) | (1u << 15) | (1u << 4)); // clear AP[1:0], AP[2], XN
    e |= ap_bits(prot) << 10;
    if (!(prot & VM_PROT_EXEC))
        e |= (1u << 4);
    return e;
}

static uint32_t l2_page_set_prot(uint32_t e, vm_prot_t prot)
{
    e &= ~((0x3u << 4) | (1u << 9) | 0x1u); // clear AP[1:0], AP[2], XN
    e |= ap_bits(prot) << 4;
    if (!(prot & VM_PROT_EXEC))
        e |= 0x1u;
    return e;
}

// TTBR value for a translation-table base PA. The walk attributes must match
// how the table memory is actually mapped (normal WBWA, inner-shareable):
// IRGN=0b11 (bits 6,0), S (bit 1), RGN=0b01 (bit 3), NOS (bit 5) — the same
// attributes _start.S boots with. A mismatch (e.g. non-shareable walks) lets
// the hardware walker read stale DRAM behind dirty shareable D-cache lines;
// QEMU doesn't model caches, so that only fails on silicon, and only once
// the first full TLB flush forces real walks of a PMM-built table.
static inline uint32_t ttbr_value(uintptr_t ttbr_pa)
{
    return (uint32_t)ttbr_pa | 0x6Bu;
}

uintptr_t arch_mmu_create_tables(addrspace_type_t type)
{
    const size_t l1_bytes = l1_table_bytes(type);
    const size_t l1_pages = l1_table_pages(type);

    // Alignment must equal table size for both user (8 KB) and kernel (16 KB).
    uintptr_t l1_pa = pmm_alloc_pages_aligned(l1_pages, l1_pages);
    if (!l1_pa)
        return 0;

    assert((l1_pa & (l1_bytes - 1)) == 0);

    memset((void *)PA_TO_VA(l1_pa), 0, l1_bytes);

    return l1_pa;
}

void arch_mmu_free_tables(uintptr_t ttbr0_pa, addrspace_type_t type)
{
    if (ttbr0_pa == 0)
    {
        return;
    }

    // Free every L2 table referenced by the L1, then the L1 pages themselves.
    // The L1 occupies l1_table_pages() contiguous PMM pages.
    uint32_t *l1 = (uint32_t *)PA_TO_VA(ttbr0_pa);
    size_t entries = l1_entry_count(type);
    size_t pages = l1_table_pages(type);

    for (size_t i = 0; i < entries; i++)
    {
        if ((l1[i] & DESC_TYPE_MASK) == DESC_L2)
        {
            uint32_t l2_pa = l1[i] & ALIGNMENT_1KB_MASK;
            l2_pool_free(l2_pa);
        }
    }

    for (size_t i = 0; i < pages; i++)
    {
        pmm_free_page(ttbr0_pa + i * PAGE_SIZE);
    }
}

bool arch_mmu_map(addrspace_t *as, uintptr_t va, uintptr_t pa, size_t size,
                  vm_prot_t prot, vm_memtype_t memtype)
{

    if (!as || size == 0)
    {
        return false;
    }

    // TTBR0 user tables only cover [0, USER_VA_TOP) when N=1.
    // Reject ranges that would index beyond the 2048-entry user table.
    if (as->type == ADDRSPACE_USER)
    {
        if (va >= USER_VA_TOP || size > (USER_VA_TOP - va))
        {
            return false;
        }
    }

    // Bring-up policy: sections only.
    if ((va % SECTION_SIZE) == 0 && (pa % SECTION_SIZE) == 0 && (size % SECTION_SIZE) == 0)
    {
        // During identity-map bring-up, TTBR0 PA is directly addressable (MMU off / identity).
        uint32_t *l1_table = (uint32_t *)PA_TO_VA(as->ttbr0_pa);
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

        return true;
    }
    else if ((va % PAGE_SIZE == 0) && (pa % PAGE_SIZE == 0) && (size % PAGE_SIZE == 0))
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
    else
    {
        return false;
    }
}

bool arch_mmu_unmap(addrspace_t *as, uintptr_t va, size_t size)
{
    if (!as || size == 0)
    {
        return false;
    }

    bool unmapped_any = false;
    bool page_mode = false;
    size_t unmapped_pages = 0;

    // Section-aligned: use sections
    if ((va % SECTION_SIZE) == 0 && (size % SECTION_SIZE) == 0)
    {
        uint32_t *l1_table = (uint32_t *)PA_TO_VA(as->ttbr0_pa);

        KDEBUG("unmap: clearing sections va=%p size=%p", (void *)va, (void *)size);
        for (uintptr_t offset = 0; offset < size; offset += SECTION_SIZE)
        {
            size_t idx = L1_IDX(va + offset);

            if (l1_table[idx] != 0)
            {
                l1_table[idx] = 0;
                unmapped_any = true;
            }
        }
        KDEBUG("unmap: sections cleared");
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
                if (size <= (16 * PAGE_SIZE))
                {
                    arch_mmu_barrier();
                    arch_mmu_flush_tlb_va(va + offset);
                }
            }
        }
    }
    else
    {
        return false;
    }

    if (unmapped_any)
    {
        // For section unmaps and larger page ranges, invalidate by ASID.
        if (!page_mode || size > (16 * PAGE_SIZE) || unmapped_pages == 0)
        {
            KDEBUG("unmap: tlb flush (asid=%u)", as->asid_token.asid);
            arch_mmu_flush_tlb_asid(as->asid_token.asid);
        }
        KDEBUG("unmap: barrier");
        arch_mmu_barrier();
        KDEBUG("unmap: done");
    }

    return unmapped_any;
}

bool arch_mmu_protect(addrspace_t *as, uintptr_t va, size_t size, vm_prot_t prot)
{
    if (!as || size == 0)
        return false;

    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->ttbr0_pa);
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
            uint32_t l2_pa = l1_entry & ALIGNMENT_1KB_MASK;
            uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_pa);
            uint32_t l2_idx = L2_IDX(curr_va);

            if (!(l2[l2_idx] & 0x2))
                continue; // not mapped

            l2[l2_idx] = l2_page_set_prot(l2[l2_idx], prot);
            changed = true;
        }
    }

    if (changed)
    {
        arch_mmu_flush_tlb_asid(as->asid_token.asid);
        arch_mmu_barrier();
    }
    return changed;
}

void arch_mmu_enable(addrspace_t *as)
{
    if (!as || as->ttbr0_pa == 0)
    {
        return;
    }

    // Barriers before changing translation context.
    arch_mmu_barrier();

    // Set TTBR0 to the L1 table base (cacheable).
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"(ttbr_value(as->ttbr0_pa)) : "memory");

    // Domain Access Control: set domain 0 to Client (no permission checks during bring-up).
    // Bits [1:0] correspond to domain 0.
    uint32_t dacr = 0x1u;
    __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" ::"r"(dacr) : "memory");

    // Invalidate TLB before enabling.
    arch_mmu_flush_tlb();
    arch_mmu_barrier();

    // Read SCTLR, set M bit.
    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= 1u; // SCTLR.M
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" ::"r"(sctlr) : "memory");

    // Synchronize after enabling MMU.
    arch_mmu_barrier();
}

void arch_mmu_switch(addrspace_t *as)
{
    if (!as || as->ttbr0_pa == 0)
    {
        return;
    }

    if (as->asid_token.generation != asid_current_generation())
    {
        asid_free(as->asid_token); // free the old ASID (no-op if already reclaimed)
        as->asid_token = asid_alloc();
    }

    // Write the ASID to the ASID register (ARMv7-A short-descriptor).
    __asm__ volatile("mcr p15, 0, %0, c13, c0, 1" ::"r"((uint32_t)as->asid_token.asid) : "memory");

    arch_mmu_barrier();

    // Write TTBR0 with the new address space's L1 table base.
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"(ttbr_value(as->ttbr0_pa)) : "memory");

    arch_mmu_barrier();

    arch_mmu_flush_tlb_asid(as->asid_token.asid);

    arch_mmu_barrier();
}

void arch_mmu_flush_tlb(void)
{
    // Invalidate entire unified TLB (ARMv7-A short-descriptor).
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

    // Invalidate unified TLB entries matching ASID (bits [7:0]).
    uint32_t asid_arg = (uint32_t)asid;
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 2" ::"r"(asid_arg) : "memory");
}

void arch_mmu_flush_tlb_va(uintptr_t va)
{
    // Invalidate unified TLB entry by MVA.
    // The architecture ignores low bits as appropriate.
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 1" ::"r"((uint32_t)(va & ALIGNMENT_4KB_MASK)) : "memory");
}

uintptr_t arch_mmu_translate(uintptr_t ttbr0_pa, uintptr_t va)
{
    if (ttbr0_pa == 0)
    {
        return 0;
    }

    uint32_t *l1_table = (uint32_t *)PA_TO_VA(ttbr0_pa);
    uint32_t l1_entry = l1_table[L1_IDX(va)];
    uint32_t type = l1_entry & DESC_TYPE_MASK;

    if (type == DESC_FAULT)
    {
        return 0; // unmapped
    }
    else if (type == DESC_SECTION || type == 0x3)
    {
        // Section (0x3 = supersection bit set; treated as section here).
        uintptr_t section_base = (uintptr_t)(l1_entry & ALIGNMENT_1MB_MASK);
        return section_base | (va & (SECTION_SIZE - 1));
    }
    else if (type == DESC_L2)
    {
        uint32_t l2_table_pa = l1_entry & ALIGNMENT_1KB_MASK;
        uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_table_pa);
        uint32_t l2_entry = l2[L2_IDX(va)];

        if (!(l2_entry & 0x2))
        {
            return 0; // not a valid small page
        }

        uint32_t page_pa = l2_entry & ALIGNMENT_4KB_MASK;
        return page_pa | (va & (PAGE_SIZE - 1));
    }
    return 0;
}

static uintptr_t arch_mmu_alloc_l2_table(void)
{
    uintptr_t new_page = l2_pool_alloc();
    if (!new_page)
        return 0;
    return (uintptr_t)new_page;
}

static uint32_t arch_mmu_make_l1_pte(uintptr_t l2_pa)
{
    if (!l2_pa)
        return 0;
    return (uint32_t)(l2_pa & ALIGNMENT_1KB_MASK) | L1_L2PTR_TAG;
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

    /* Extract physical base (bits [31:20]) */
    uintptr_t section_pa = section & ALIGNMENT_1MB_MASK;

    /* Transcode access/attribute bits from section to small-page positions.
     * Section: XN[4], B/C[3:2], AP[11:10], TEX[14:12], AP2[15]
     * Small page: XN[0], B/C[3:2], AP[5:4], TEX[8:6], AP2[9] */
    uint32_t sec_xn = (section >> 4) & 0x1;
    uint32_t sec_cb = (section >> 2) & 0x3;
    uint32_t sec_ap = (section >> 10) & 0x3;
    uint32_t sec_tex = (section >> 12) & 0x7;
    uint32_t sec_ap2 = (section >> 15) & 0x1;

    /* Allocate an L2 table (1KB, from the pool) */
    uintptr_t l2_pa = arch_mmu_alloc_l2_table();
    if (!l2_pa)
        return false;

    uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_pa);

    /* Fill all entries to replicate the section mapping at 4KB granularity */
    for (uint32_t i = 0; i < L2_ENTRIES; i++)
    {
        uint32_t page_entry = (uint32_t)((section_pa + i * PAGE_SIZE) & ALIGNMENT_4KB_MASK) | L2_SMALL_TAG;

        page_entry |= sec_xn;         /* XN -> bit 0 */
        page_entry |= (sec_cb << 2);  /* B/C -> bits [3:2] */
        page_entry |= (sec_ap << 4);  /* AP[1:0] -> bits [5:4] */
        page_entry |= (sec_tex << 6); /* TEX[2:0] -> bits [8:6] */
        page_entry |= (sec_ap2 << 9); /* AP[2] -> bit 9 */

        l2[i] = page_entry;
    }

    /* Replace the section entry with an L1 page-table descriptor */
    l1[l1_idx] = arch_mmu_make_l1_pte(l2_pa);

    /* Flush TLB — the old section TLB entries are now stale */
    arch_mmu_flush_tlb_asid(asid);
    arch_mmu_barrier();

    return true;
}

static bool arch_mmu_map_page(addrspace_t *as, uintptr_t va, uintptr_t pa,
                              vm_memtype_t memtype, vm_prot_t prot)
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

    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->ttbr0_pa);

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
        uint32_t l2_table_pa = l1_entry & ALIGNMENT_1KB_MASK;
        l2 = (uint32_t *)PA_TO_VA(l2_table_pa);
    }
    else
    {
        // Section mapping - break it into page entries first
        if (!arch_mmu_break_section(l1, l1_idx, as->asid_token.asid))
            return false;

        uint32_t l2_table_pa = l1[l1_idx] & ALIGNMENT_1KB_MASK;
        l2 = (uint32_t *)PA_TO_VA(l2_table_pa);
    }

    // Now install the page entry
    l2[l2_idx] = l2_page_desc(pa, prot, memtype);
    return true;
}

bool arch_mmu_unmap_page(addrspace_t *as, uintptr_t va)
{
    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->ttbr0_pa);

    uint32_t l1_idx = L1_IDX(va);
    uint32_t l2_idx = L2_IDX(va);

    uint32_t l1_entry = l1[l1_idx];
    uint32_t type = l1_entry & DESC_TYPE_MASK;

    uint32_t *l2;

    if (type == DESC_L2)
    {
        uint32_t l2_table_pa = l1_entry & ALIGNMENT_1KB_MASK;
        l2 = (uint32_t *)PA_TO_VA(l2_table_pa);
    }
    else
    {
        return false; // Not a page table, can't unmap page
    }

    if (l2[l2_idx] == 0)
    {
        return false; // wasn't mapped
    }
    l2[l2_idx] = 0;
    return true;
}

static vm_owner_t mmu_region_owner_for_va(const addrspace_t *as, uintptr_t va)
{
    if (!as)
        return VM_OWNER_ANON;

    for (uint32_t i = 0; i < as->regions.len; i++)
    {
        const vm_region_t *r = vm_region_vec_get((vm_region_vec_t *)&as->regions, i);
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

void arch_mmu_free_user_pages(addrspace_t *as)
{
    if (!as)
        return;

    uintptr_t ttbr0_pa = as->ttbr0_pa;
    uint32_t *l1 = (uint32_t *)PA_TO_VA(ttbr0_pa);
    const uint32_t small_page_attr_mask = (0x7u << 6) | (1u << 3) | (1u << 2);
    const uint32_t device_attr = (1u << 2); // TEX=000, C=0, B=1

    // Walk the user range of the L1. Only free the BACKING physical pages,
    // not the page-table structures: L2 tables and L1 pages are freed
    // separately by arch_mmu_free_tables().
    size_t entries = l1_entry_count(ADDRSPACE_USER);
    for (size_t i = 0; i < entries; i++)
    {
        uint32_t l1_entry = l1[i];

        if ((l1_entry & DESC_TYPE_MASK) == DESC_L2)
        {
            // L2 page table — walk it and free each mapped user page
            uintptr_t l2_pa = l1_entry & ALIGNMENT_1KB_MASK;
            uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_pa);

            for (size_t j = 0; j < L2_ENTRIES; j++)
            {
                if (l2[j] & 0x2)
                { // valid small page
                    uintptr_t va = ((uintptr_t)i << 20) | ((uintptr_t)j << 12);

                    // Keep shared kernel-exported syspage mapped read-only at user VA 0x1000.
                    if (va == 0x1000u)
                        continue;

                    // Only reclaim pages owned by this address space.
                    if (mmu_region_owner_for_va(as, va) != VM_OWNER_ANON)
                        continue;

                    // Device mappings are not PMM-owned pages.
                    if ((l2[j] & small_page_attr_mask) == device_attr)
                        continue;

                    uintptr_t page_pa = l2[j] & ALIGNMENT_4KB_MASK;
                    pmm_free_page(page_pa);
                }
            }
        }
        // Section mappings (DESC_SECTION): not used for user space currently
    }
}

void arch_mmu_init_ttbr1(addrspace_t *as)
{
    // Mirror the kernel L1 into TTBR1, then set TTBCR.N=1 to split at 0x80000000.
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 1" ::"r"(ttbr_value(as->ttbr0_pa)) : "memory");
    uint32_t ttbcr;
    __asm__ volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr)::"memory"); // get TTBCR (translation table base control register)
    ttbcr &= 0xFFFFFFE0;                                                   // clear last 5 bits
    ttbcr |= 0x1;                                                          // N=1: split at 0x80000000, clear PD0/PD1
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 2" ::"r"(ttbcr) : "memory"); // set
    arch_mmu_flush_tlb();                                                  // flush tlb so it doesnt corrupt anything
    arch_mmu_barrier();                                                    // make sure it goes through
}

void arch_mmu_barrier(void)
{
    // completion of memory operations
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}
