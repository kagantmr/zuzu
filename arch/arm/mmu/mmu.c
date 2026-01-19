#include "mmu.h"
#include "kernel/mm/pmm.h"
#include "lib/mem.h"
#include <stdint.h>


uintptr_t arch_mmu_create_tables(void) {
    // ARMv7 short-descriptor L1 table requirements:
    // - 16KB size
    // - 16KB alignment (TTBR0 base must be 0 mod 16KB)
    const size_t l1_bytes = 16 * 1024;
    const size_t l1_pages = 4;
    const uintptr_t l1_align = 16 * 1024;

    // alloc_pages() returns a PHYSICAL address in PMM (see pmm.c).
    // Retry until we get a 16KB-aligned base.
    for (int attempt = 0; attempt < 256; attempt++) {
        uintptr_t l1_pa = pmm_alloc_pages(l1_pages);
        if (l1_pa == 0) {
            return 0;
        }

        if ((l1_pa & (l1_align - 1)) != 0) {
            // Not 16KB-aligned: free the 4 pages individually and retry.
            for (size_t i = 0; i < l1_pages; i++) {
                (void)pmm_free_page(l1_pa + i * PAGE_SIZE);
            }
            continue;
        }

        // MMU is off during bring-up and we will identity-map initially,
        // so treating PA as an addressable pointer is OK here.
        memset((void*)l1_pa, 0, l1_bytes);
        return l1_pa;
    }

    // Could not obtain a 16KB-aligned block (fragmentation / allocator limits).
    return 0;
}

void arch_mmu_free_tables(uintptr_t ttbr0_pa) {
    if (ttbr0_pa == 0) {
        return;
    }

    // L1 table is exactly 4 contiguous pages allocated via alloc_pages(4)
    // alloc_pages() returns a PHYSICAL address
    for (size_t i = 0; i < 4; i++) {
        pmm_free_page(ttbr0_pa + i * PAGE_SIZE);
    }
}


bool arch_mmu_map(addrspace_t* as, uintptr_t va, uintptr_t pa, size_t size,
                  vm_prot_t prot, vm_memtype_t memtype) {
    (void)prot;

    // Bring-up policy: sections only.
    if (!as || size == 0 || (va % 0x100000) != 0 || (pa % 0x100000) != 0 || (size % 0x100000) != 0) {
        return false;
    }

    // During identity-map bring-up, TTBR0 PA is directly addressable (MMU off / identity).
    uint32_t* l1_table = (uint32_t*)(as->ttbr0_pa);

    for (uintptr_t offset = 0; offset < size; offset += 0x100000) {
        uintptr_t curr_va = va + offset;
        uintptr_t curr_pa = pa + offset;

        // Base section descriptor: section type (bits[1:0] = 0b10)
        uint32_t entry = (uint32_t)(curr_pa & 0xFFF00000) | 0x2;

        // Domain = 0 (bits[8:5] left as 0)

        // Bring-up permissions: AP[1:0] = 0b11 (full access)
        entry |= (0x3u << 10);

        // Memory attributes (short-descriptor section): TEX[14:12], C[3], B[2]
        // For bring-up we keep NORMAL memory non-cacheable and DEVICE memory as device.
        if (memtype == VM_MEM_DEVICE) {
            // Device: TEX=0, C=0, B=1
            entry |= (1u << 2);
        } else {
            // Normal non-cacheable: TEX=0, C=0, B=0
        }

        size_t idx = (curr_va >> 20) & 0xFFF;
        l1_table[idx] = entry;
    }

    return true;
}


bool arch_mmu_unmap(addrspace_t* as, uintptr_t va, size_t size) {
    // Bring-up policy: sections only.
    if (!as || size == 0 || (va % 0x100000) != 0 || (size % 0x100000) != 0) {
        return false;
    }

    // During identity-map bring-up, TTBR0 PA is directly addressable (MMU off / identity).
    uint32_t* l1_table = (uint32_t*)(as->ttbr0_pa);

    bool unmapped_any = false;
    for (uintptr_t offset = 0; offset < size; offset += 0x100000) {
        uintptr_t curr_va = va + offset;
        size_t idx = (curr_va >> 20) & 0xFFF;

        if (l1_table[idx] != 0) {
            l1_table[idx] = 0; // fault
            unmapped_any = true;
        }
    }

    // Conservative bring-up behavior: flush the entire TLB if anything changed.
    if (unmapped_any) {
        arch_mmu_flush_tlb();
        arch_mmu_barrier();
    }

    return unmapped_any;
}



bool arch_mmu_protect(addrspace_t* as, uintptr_t va, size_t size, vm_prot_t prot) {
    (void)prot;

    // Bring-up policy: sections only. Permissions are currently permissive.
    if (!as || size == 0 || (va % 0x100000) != 0 || (size % 0x100000) != 0) {
        return false;
    }

    // For bring-up we do not yet encode per-region permissions (XN/user/RO).
    // Return true iff the range is currently mapped.
    uint32_t* l1_table = (uint32_t*)(as->ttbr0_pa);
    bool found_any = false;

    for (uintptr_t offset = 0; offset < size; offset += 0x100000) {
        uintptr_t curr_va = va + offset;
        size_t idx = (curr_va >> 20) & 0xFFF;
        if (l1_table[idx] != 0) {
            found_any = true;
        }
    }

    // No changes applied in bring-up mode.
    return found_any;
}



void arch_mmu_enable(addrspace_t* as) {
    if (!as || as->ttbr0_pa == 0) {
        return;
    }

    // Barriers before changing translation context.
    arch_mmu_barrier();

    // Set TTBR0 to the L1 table base.
    // For bring-up we do not set cacheability bits in TTBR0.
    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" :: "r"((uint32_t)as->ttbr0_pa) : "memory");

    // Domain Access Control: set domain 0 to Manager (no permission checks during bring-up).
    // Bits [1:0] correspond to domain 0.
    uint32_t dacr = 0x3u;
    __asm__ volatile("mcr p15, 0, %0, c3, c0, 0" :: "r"(dacr) : "memory");

    // Invalidate TLB before enabling.
    arch_mmu_flush_tlb();
    arch_mmu_barrier();

    // Read SCTLR, set M bit.
    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= 1u; // SCTLR.M
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory");

    // Synchronize after enabling MMU.
    arch_mmu_barrier();
}



void arch_mmu_disable(void) {
    // Disable MMU by clearing SCTLR.M.
    uint32_t sctlr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr &= ~1u;
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory");

    arch_mmu_flush_tlb();
    arch_mmu_barrier();
}


void arch_mmu_switch(addrspace_t* as) {
    if (!as || as->ttbr0_pa == 0) {
        return;
    }

    arch_mmu_barrier();

    __asm__ volatile("mcr p15, 0, %0, c2, c0, 0" :: "r"((uint32_t)as->ttbr0_pa) : "memory");

    arch_mmu_flush_tlb();
    arch_mmu_barrier();
}



void arch_mmu_flush_tlb(void) {
    // Invalidate entire unified TLB (ARMv7-A short-descriptor).
    uint32_t zero = 0;
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(zero) : "memory");
}


void arch_mmu_flush_tlb_va(uintptr_t va) {
    // Invalidate unified TLB entry by MVA.
    // The architecture ignores low bits as appropriate.
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 1" :: "r"((uint32_t)va) : "memory");
}



uintptr_t arch_mmu_translate(uintptr_t ttbr0_pa, uintptr_t va) {
    if (ttbr0_pa == 0) {
        return 0;
    }

    // Section-only translation (L1 short-descriptor)
    uint32_t* l1_table = (uint32_t*)ttbr0_pa;
    size_t idx = (va >> 20) & 0xFFF;
    uint32_t desc = l1_table[idx];

    // If not a section descriptor, treat as unmapped for bring-up.
    if ((desc & 0x3u) != 0x2u) {
        return 0;
    }

    uintptr_t section_base = (uintptr_t)(desc & 0xFFF00000u);
    return section_base | (va & 0xFFFFFu);
}



void arch_mmu_barrier(void) {
    // Ensure completion of memory operations and pipeline synchronization.
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}
