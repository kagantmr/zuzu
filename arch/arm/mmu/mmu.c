#include "mmu.h"
#include "kernel/mm/pmm.h"
#include "lib/mem.h"
#include "core/assert.h"
#include <stdint.h>

#define L1_MASK 0x2u
#define L2_MASK 0x1u

#define ALIGNMENT_1MB_MASK 0xFFF00000u
#define ALIGNMENT_1KB_MASK 0xFFFFFC00u
#define ALIGNMENT_4KB_MASK 0xFFFFF000u

//#define SECTION_SIZE 0x100000

uintptr_t arch_mmu_create_tables(void)
{
    const size_t   l1_bytes = 16 * 1024;
    const size_t   l1_pages = l1_bytes / PAGE_SIZE;   // 4
    const uintptr_t l1_align = 16 * 1024;             // TTBR0 alignment requirement
    const size_t   l1_align_pages = l1_align / PAGE_SIZE; // 4

    uintptr_t l1_pa = pmm_alloc_pages_aligned(l1_pages, l1_align_pages);
    if (!l1_pa) return 0;

    kassert((l1_pa & (l1_align - 1)) == 0);

    // If MMU is off or identity mapping exists for this region, this is OK.
    memset((void *)PA_TO_VA(l1_pa), 0, l1_bytes);  // ← goes through kernel mapping

    return l1_pa;
}

void arch_mmu_free_tables(uintptr_t ttbr0_pa)
{
    if (ttbr0_pa == 0)
    {
        return;
    }

    // L1 table is exactly 4 contiguous pages allocated via alloc_pages(4)
    // alloc_pages() returns a PHYSICAL address

    uint32_t *l1 = (uint32_t *)PA_TO_VA(ttbr0_pa);

    for (size_t page_no = 0; page_no < 2048; page_no++)
    {
        if ((l1[page_no] & 0x3) == 0x1) {  // if L2 page table
            uint32_t l2_table_pa = l1[page_no] & 0xFFFFFC00;
            pmm_free_page(l2_table_pa);
        }
    }

    for (size_t i = 0; i < 4; i++)
    {
        pmm_free_page(ttbr0_pa + i * PAGE_SIZE);
    }
}

bool arch_mmu_map(addrspace_t *as, uintptr_t va, uintptr_t pa, size_t size,
                  vm_prot_t prot, vm_memtype_t memtype)
{

    if (!as || size == 0) {
        return false;
    }

    // Bring-up policy: sections only.
    if ((va % SECTION_SIZE) == 0 && (pa % SECTION_SIZE) == 0 && (size % SECTION_SIZE) == 0)
    {
        // During identity-map bring-up, TTBR0 PA is directly addressable (MMU off / identity).
        uint32_t *l1_table = (uint32_t *)PA_TO_VA(as->ttbr0_pa);

        for (uintptr_t offset = 0; offset < size; offset += SECTION_SIZE)
        {
            uintptr_t curr_va = va + offset;
            uintptr_t curr_pa = pa + offset;

            // Base section descriptor: section type (bits[1:0] = 0b10)
            uint32_t entry = (uint32_t)(curr_pa & ALIGNMENT_1MB_MASK) | L1_MASK;

            // Domain = 0 (bits[8:5] left as 0)

            // Bring-up permissions: AP[1:0] = 0b01 (kernel code)
            entry |= (0x1u << 10);

            // Memory attributes (short-descriptor section): TEX[14:12], C[3], B[2]
            // For bring-up we keep NORMAL memory non-cacheable and DEVICE memory as device.
            if (memtype == VM_MEM_DEVICE)
            {
                // Device: TEX=0, C=0, B=1
                entry |= (1u << 2);
            }
            else
            {
            // Enable Write-Back, Write-Allocate Caching
                // TEX=001 (bit 12), C=1 (bit 3), B=1 (bit 2)
                entry |= (1u << 12) | (1u << 3) | (1u << 2);
            }

            size_t idx = (curr_va >> 20) & 0xFFF;
            l1_table[idx] = entry;
        }

        return true;
    } else if ((va % PAGE_SIZE == 0) && (pa % PAGE_SIZE == 0) && (size % PAGE_SIZE == 0)) {
        // Loop over each page
        for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE)
        {
            if (!arch_mmu_map_page(as, va + offset, pa + offset, memtype, prot)) {
            // Unmap everything that was mapped so far
            for (uintptr_t rollback = 0; rollback < offset; rollback += PAGE_SIZE) {
                arch_mmu_unmap_page(as, va + rollback);
            }
            return false;
            }
        }

        return true;
    } else {
        return false; // couldn't map 
    }
}

bool arch_mmu_unmap(addrspace_t *as, uintptr_t va, size_t size)
{
    if (!as || size == 0) {
        return false;
    }

    bool unmapped_any = false;

    // Section-aligned: use sections
    if ((va % SECTION_SIZE) == 0 && (size % SECTION_SIZE) == 0)
    {
        uint32_t *l1_table = (uint32_t *)PA_TO_VA(as->ttbr0_pa);

        for (uintptr_t offset = 0; offset < size; offset += SECTION_SIZE)
        {
            uintptr_t curr_va = va + offset;
            size_t idx = (curr_va >> 20) & 0xFFF;

            if (l1_table[idx] != 0)
            {
                l1_table[idx] = 0;
                unmapped_any = true;
            }
        }
    }
    // Page-aligned: use pages
    else if ((va % PAGE_SIZE) == 0 && (size % PAGE_SIZE) == 0)
    {
        for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE)
        {
            if (arch_mmu_unmap_page(as, va + offset)) {
                unmapped_any = true;
            }
        }
    }
    else
    {
        return false;
    }

    if (unmapped_any)
    {
        arch_mmu_flush_tlb();
        arch_mmu_barrier();
    }

    return unmapped_any;
}

bool arch_mmu_protect(addrspace_t *as, uintptr_t va, size_t size, vm_prot_t prot)
{
    (void)prot;

    // Bring-up policy: sections only. Permissions are currently permissive.
    if (!as || size == 0 || (va % SECTION_SIZE) != 0 || (size % SECTION_SIZE) != 0)
    {
        return false;
    }

    // For bring-up we do not yet encode per-region permissions (XN/user/RO).
    // Return true iff the range is currently mapped.
    uint32_t *l1_table = (uint32_t *)PA_TO_VA(as->ttbr0_pa);
    bool found_any = false;

    for (uintptr_t offset = 0; offset < size; offset += SECTION_SIZE)
    {
        uintptr_t curr_va = va + offset;
        size_t idx = (curr_va >> 20) & 0xFFF;
        if (l1_table[idx] != 0)
        {
            found_any = true;
        }
    }

    // No changes applied in bring-up mode.
    return found_any;
}

void arch_mmu_enable(addrspace_t *as)
{
    if (!as || as->ttbr0_pa == 0)
    {
        return;
    }

    // Barriers before changing translation context.
    arch_mmu_barrier();

    // Set TTBR0 to the L1 table base.
    // For bring-up we do not set cacheability bits in TTBR0.
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"((uint32_t)as->ttbr0_pa) : "memory");

    // Domain Access Control: set domain 0 to Manager (no permission checks during bring-up).
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

void arch_mmu_disable(void)
{
    // Disable MMU by clearing SCTLR.M.
    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr &= ~1u;
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" ::"r"(sctlr) : "memory");

    arch_mmu_flush_tlb();
    arch_mmu_barrier();
}

void arch_mmu_switch(addrspace_t *as)
{
    if (!as || as->ttbr0_pa == 0)
    {
        return;
    }

    arch_mmu_barrier();

    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" ::"r"((uint32_t)as->ttbr0_pa) : "memory");

    arch_mmu_flush_tlb();
    arch_mmu_barrier();
}

void arch_mmu_flush_tlb(void)
{
    // Invalidate entire unified TLB (ARMv7-A short-descriptor).
    uint32_t zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" ::"r"(zero) : "memory");
}

void arch_mmu_flush_tlb_va(uintptr_t va)
{
    // Invalidate unified TLB entry by MVA.
    // The architecture ignores low bits as appropriate.
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 1" ::"r"((uint32_t)va) : "memory");
}

uintptr_t arch_mmu_translate(uintptr_t ttbr0_pa, uintptr_t va)
{
    if (ttbr0_pa == 0)
    {
        return 0;
    }

    // Section-only translation (L1 short-descriptor)
    uint32_t *l1_table = (uint32_t *)PA_TO_VA(ttbr0_pa);
    size_t idx = (va >> 20) & 0xFFF;
    uint32_t l1_entry = l1_table[idx];
    uint32_t type = l1_entry & 0x3;

    if (type == 0) {
        // Fault - unmapped
        return 0;
    }
    else if (type == 2 || type == 3) {
        // Section - your existing code
        uintptr_t section_base = (uintptr_t)(l1_entry & 0xFFF00000u);
        return section_base | (va & 0xFFFFFu);
    }
    else if (type == 1) {
        uint32_t l2_table_pa = l1_entry & 0xFFFFFC00;
        uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_table_pa);
        uint32_t l2_idx = (va >> 12) & 0xFF;
        uint32_t l2_entry = l2[l2_idx];
        
        if (!(l2_entry & 0x2)) {
            return 0;  // not a valid small page
        }
        
        uint32_t page_pa = l2_entry & ALIGNMENT_4KB_MASK;
        return page_pa | (va & 0xFFF);
    }
    return 0;
}

uintptr_t arch_mmu_alloc_l2_table(void) {
    uintptr_t new_page = pmm_alloc_page();
    if (!new_page) return 0;
    memset((void *)PA_TO_VA(new_page), 0, 4096);
    return (uintptr_t)new_page;
}

uint32_t arch_mmu_make_l1_pte(uintptr_t l2_pa) {
    if (!l2_pa) return 0;
    return (uint32_t)(l2_pa & ALIGNMENT_1KB_MASK) | L2_MASK; // L2 mask
}

uint32_t arch_mmu_make_l2_pte(uintptr_t pa, vm_memtype_t memtype, vm_prot_t prot) {
    // Start with PA masked for 4KB alignment, plus type bits
    uint32_t entry = (pa & ALIGNMENT_4KB_MASK) | 0x2;
    // Set AP bits depending on who's demanding the page (11 means everyone can access, 01 means privileged)
    if (prot & VM_PROT_USER) {
        entry |= (3u << 4);
    } else {
        entry |= (1u << 4);
    }
   
    // 3. Add memory type bits (TEX/C/B)
    if (memtype == VM_MEM_DEVICE) {
        entry |= (1u << 2);                           // B=1 only → Device
    } else {
        entry |= (1u << 6) | (1u << 3) | (1u << 2);  // TEX=001, C=1, B=1 → Normal cached
    }
    return entry;
}



bool arch_mmu_map_page(addrspace_t *as, uintptr_t va, uintptr_t pa,
                       vm_memtype_t memtype, vm_prot_t prot) {
    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->ttbr0_pa);
    
    uint32_t l1_idx = (va >> 20) & 0xFFF;   // bits [31:20] → index 0-4095
    uint32_t l2_idx = (va >> 12) & 0xFF;    // bits [19:12] → index 0-255
    
    uint32_t l1_entry = l1[l1_idx];
    uint32_t type = l1_entry & 0x3;
    
    uint32_t *l2;
    
    if (type == 0) {
        uintptr_t l2_pa = arch_mmu_alloc_l2_table();
        if (!l2_pa) return false;  // allocation failed
        
        l1[l1_idx] = arch_mmu_make_l1_pte(l2_pa);
        l2 = (uint32_t *)PA_TO_VA(l2_pa);
    } else if (type == 1) {
        uint32_t l2_table_pa = l1_entry & 0xFFFFFC00;
        l2 = (uint32_t *)PA_TO_VA(l2_table_pa);
    } else {
        // It's a section - can't mix!
        return false;
    }
    
    // Now install the page entry
    l2[l2_idx] = arch_mmu_make_l2_pte(pa, memtype, prot);
    return true;
}

bool arch_mmu_unmap_page(addrspace_t *as, uintptr_t va) {
    uint32_t *l1 = (uint32_t *)PA_TO_VA(as->ttbr0_pa);
    
    uint32_t l1_idx = (va >> 20) & 0xFFF;   // bits [31:20] → index 0-4095
    uint32_t l2_idx = (va >> 12) & 0xFF;    // bits [19:12] → index 0-255
    
    uint32_t l1_entry = l1[l1_idx];
    uint32_t type = l1_entry & 0x3;

    uint32_t *l2;
    
    if (type == 1) {
        uint32_t l2_table_pa = l1_entry & 0xFFFFFC00;
        l2 = (uint32_t *)PA_TO_VA(l2_table_pa);
    } else {
        return false;  // Not a page table, can't unmap page
    }

    if (l2[l2_idx] == 0) {
        return false;  // wasn't mapped
    }
    l2[l2_idx] = 0;
    return true;
}

void arch_mmu_init_ttbr1(addrspace_t *as) {
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 1" ::"r"((uint32_t)as->ttbr0_pa) : "memory"); // write ttbr0 to ttbr1, final operand 1 because we're writing to ttbr1
    uint32_t ttbcr;
    __asm__ volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr) :: "memory"); // get TTBCR (translation table base control register)
    ttbcr &= 0xFFFFFFE0; // clear last 5 bits 
    ttbcr |= 0x1; // write 00001 (set N=1, split at split at 0x80000000, clear PD0 and PD1)
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 2" :: "r"(ttbcr) : "memory"); // set
    arch_mmu_flush_tlb(); // flush tlb so it doesnt corrupt shit
    arch_mmu_barrier(); // make sure it goes through or whatever man
}

void arch_mmu_barrier(void)
{
    // Ensure completion of memory operations and pipeline synchronization.
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

