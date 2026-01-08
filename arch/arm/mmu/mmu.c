#include "mmu.h"
#include "kernel/mm/alloc.h"
#include "lib/mem.h"
#include <stdint.h>

// ARM v7 (ARMv7-A) page table constants
#define L1_DESCRIPTOR_BASE_MASK     0xFFFFF000
#define L1_DESCRIPTOR_TYPE_MASK     0x00000003
#define L1_DESCRIPTOR_TYPE_INVALID  0x00
#define L1_DESCRIPTOR_TYPE_TABLE    0x01  // L2 table (coarse)
#define L1_DESCRIPTOR_TYPE_SECTION  0x02  // 1 MB section

#define L2_DESCRIPTOR_BASE_MASK     0xFFFFF000
#define L2_DESCRIPTOR_TYPE_MASK     0x00000003
#define L2_DESCRIPTOR_TYPE_INVALID  0x00
#define L2_DESCRIPTOR_TYPE_LARGE    0x01  // 64 KB large page
#define L2_DESCRIPTOR_TYPE_SMALL    0x02  // 4 KB small page

#define PAGE_SIZE 4096
#define SECTION_SIZE (1024 * 1024)  // 1 MB
#define L2_ENTRIES 256  // 256 small pages (256 * 4KB = 1 MB)

// Helper: Get L1 index from virtual address
static inline size_t l1_index(uintptr_t va) {
    return (va >> 20) & 0xFFF;  // Bits [31:20]
}

// Helper: Get L2 index from virtual address
static inline size_t l2_index(uintptr_t va) {
    return (va >> 12) & 0xFF;   // Bits [19:12]
}

// Helper: Get page offset within 4KB page
static inline size_t page_offset(uintptr_t va) {
    return va & 0xFFF;           // Bits [11:0]
}

/**
 * @brief Translate a virtual address to physical address using page tables.
 * Walks L1 table, finds L2 table, and returns physical address.
 */
uintptr_t arch_mmu_translate(uintptr_t ttbr0_pa, uintptr_t va) {
    if (ttbr0_pa == 0) return 0;
    
    // Index into L1 table
    size_t l1_idx = l1_index(va);
    
    // L1 table is at physical address ttbr0_pa
    // We need to access it via physical memory. For now, assume identity mapping
    // (PA == VA) for early boot, or use a temporary mapping.
    // This is a simplified implementation; real code would need VA of L1 table.
    uint32_t* l1_table = (uint32_t*)(ttbr0_pa);
    
    uint32_t l1_desc = l1_table[l1_idx];
    
    // Check L1 descriptor type
    if ((l1_desc & L1_DESCRIPTOR_TYPE_MASK) == L1_DESCRIPTOR_TYPE_INVALID) {
        return 0;  // Not mapped
    }
    
    if ((l1_desc & L1_DESCRIPTOR_TYPE_MASK) == L1_DESCRIPTOR_TYPE_SECTION) {
        // 1 MB section mapping
        uintptr_t section_base = l1_desc & L1_DESCRIPTOR_BASE_MASK;
        return section_base | (va & 0xFFFFF);  // Combine base with offset
    }
    
    if ((l1_desc & L1_DESCRIPTOR_TYPE_MASK) == L1_DESCRIPTOR_TYPE_TABLE) {
        // Points to L2 table
        uintptr_t l2_table_pa = l1_desc & L1_DESCRIPTOR_BASE_MASK;
        uint32_t* l2_table = (uint32_t*)(l2_table_pa);
        
        size_t l2_idx = l2_index(va);
        uint32_t l2_desc = l2_table[l2_idx];
        
        // Check L2 descriptor type
        if ((l2_desc & L2_DESCRIPTOR_TYPE_MASK) == L2_DESCRIPTOR_TYPE_INVALID) {
            return 0;  // Not mapped
        }
        
        if ((l2_desc & L2_DESCRIPTOR_TYPE_MASK) == L2_DESCRIPTOR_TYPE_SMALL) {
            // 4 KB small page
            uintptr_t page_base = l2_desc & L2_DESCRIPTOR_BASE_MASK;
            return page_base | page_offset(va);
        }
        
        if ((l2_desc & L2_DESCRIPTOR_TYPE_MASK) == L2_DESCRIPTOR_TYPE_LARGE) {
            // 64 KB large page
            uintptr_t large_base = l2_desc & 0xFFFF0000;
            return large_base | (va & 0xFFFF);
        }
    }
    
    return 0;  // Not mapped or invalid
}

/**
 * @brief Create and initialize L1 page table.
 * Allocates 16 KB of memory and zeros it.
 */
uintptr_t arch_mmu_create_tables(void) {
    // Allocate 16 KB for L1 table (4096 entries × 4 bytes)
    // This is 4 pages (4096 is PAGE_SIZE)
    uintptr_t l1_pa = (uintptr_t)kmalloc(16384);
    if (l1_pa == 0) {
        return 0;
    }
    
    // Zero the table
    memset((void*)l1_pa, 0, 16384);
    
    return l1_pa;
}

/**
 * @brief Free page tables and all associated L2 tables.
 */
void arch_mmu_free_tables(uintptr_t ttbr0_pa) {
    if (ttbr0_pa == 0) return;
    
    // Walk L1 table and free all L2 tables
    uint32_t* l1_table = (uint32_t*)(ttbr0_pa);
    
    for (size_t i = 0; i < 4096; i++) {
        uint32_t l1_desc = l1_table[i];
        
        if ((l1_desc & L1_DESCRIPTOR_TYPE_MASK) == L1_DESCRIPTOR_TYPE_TABLE) {
            uintptr_t l2_pa = l1_desc & L1_DESCRIPTOR_BASE_MASK;
            kfree((void*)l2_pa);
        }
    }
    
    // Free L1 table
    kfree((void*)ttbr0_pa);
}

/**
 * @brief Map a VA range to PA range.
 * Creates L2 tables as needed.
 */
bool arch_mmu_map(addrspace_t* as, uintptr_t va, uintptr_t pa, size_t size,
                  vm_prot_t prot, vm_memtype_t memtype) {
    (void)memtype;  // TODO: implement memory type handling (TEX/C/B bits)
    if (!as || size == 0) return false;
    
    uint32_t* l1_table = (uint32_t*)(as->ttbr0_pa);
    
    // For simplicity, map page-by-page (4 KB)
    for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uintptr_t v = va + offset;
        uintptr_t p = pa + offset;
        
        size_t l1_idx = l1_index(v);
        uint32_t l1_desc = l1_table[l1_idx];
        
        uint32_t* l2_table = NULL;
        
        // Check if L2 table exists
        if ((l1_desc & L1_DESCRIPTOR_TYPE_MASK) == L1_DESCRIPTOR_TYPE_TABLE) {
            l2_table = (uint32_t*)(l1_desc & L1_DESCRIPTOR_BASE_MASK);
        } else if ((l1_desc & L1_DESCRIPTOR_TYPE_MASK) == L1_DESCRIPTOR_TYPE_INVALID) {
            // Need to create L2 table
            l2_table = (uint32_t*)kmalloc(1024);  // 256 entries × 4 bytes
            if (!l2_table) return false;
            
            memset(l2_table, 0, 1024);
            
            // Set L1 entry to point to L2 table (coarse table descriptor)
            l1_table[l1_idx] = ((uintptr_t)l2_table & L1_DESCRIPTOR_BASE_MASK) | L1_DESCRIPTOR_TYPE_TABLE;
        } else {
            // L1 entry is a section, can't create L2 table
            return false;
        }
        
        // Now write L2 entry (small page descriptor)
        size_t l2_idx = l2_index(v);
        
        // Build L2 descriptor with AP bits and permissions
        uint32_t l2_desc = (p & L2_DESCRIPTOR_BASE_MASK) | L2_DESCRIPTOR_TYPE_SMALL;
        
        // AP bits for permissions (simplified: 0x3 = full access, 0x1 = RO)
        if ((prot & VM_PROT_WRITE) != 0) {
            l2_desc |= (0x3 << 4);  // AP[1:0] = 11 (full access)
        } else {
            l2_desc |= (0x1 << 4);  // AP[1:0] = 01 (read-only)
        }
        
        // XN bit: set if execute is NOT requested
        if ((prot & VM_PROT_EXEC) == 0) {
            l2_desc |= (1 << 0);  // XN bit
        }
        
        // nG bit (not global) - set for user mappings
        if ((prot & VM_PROT_USER) != 0) {
            l2_desc |= (1 << 11);  // nG bit
        }
        
        l2_table[l2_idx] = l2_desc;
    }
    
    return true;
}

/**
 * @brief Unmap a VA range.
 * Clears page table entries.
 */
bool arch_mmu_unmap(addrspace_t* as, uintptr_t va, size_t size) {
    if (!as || size == 0) return false;
    
    uint32_t* l1_table = (uint32_t*)(as->ttbr0_pa);
    bool found = false;
    
    for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uintptr_t v = va + offset;
        
        size_t l1_idx = l1_index(v);
        uint32_t l1_desc = l1_table[l1_idx];
        
        if ((l1_desc & L1_DESCRIPTOR_TYPE_MASK) != L1_DESCRIPTOR_TYPE_TABLE) {
            continue;  // No L2 table
        }
        
        uint32_t* l2_table = (uint32_t*)(l1_desc & L1_DESCRIPTOR_BASE_MASK);
        size_t l2_idx = l2_index(v);
        
        if (l2_table[l2_idx] != 0) {
            l2_table[l2_idx] = 0;  // Clear the entry
            found = true;
        }
    }
    
    // Invalidate TLB
    if (found) {
        arch_mmu_flush_tlb_va(va);
    }
    
    return found;
}

/**
 * @brief Change permissions on a mapped range.
 */
bool arch_mmu_protect(addrspace_t* as, uintptr_t va, size_t size, vm_prot_t new_prot) {
    if (!as || size == 0) return false;
    
    uint32_t* l1_table = (uint32_t*)(as->ttbr0_pa);
    bool found = false;
    
    for (uintptr_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uintptr_t v = va + offset;
        
        size_t l1_idx = l1_index(v);
        uint32_t l1_desc = l1_table[l1_idx];
        
        if ((l1_desc & L1_DESCRIPTOR_TYPE_MASK) != L1_DESCRIPTOR_TYPE_TABLE) {
            continue;
        }
        
        uint32_t* l2_table = (uint32_t*)(l1_desc & L1_DESCRIPTOR_BASE_MASK);
        size_t l2_idx = l2_index(v);
        uint32_t* l2_desc_ptr = &l2_table[l2_idx];
        
        if (*l2_desc_ptr == 0) continue;
        
        // Modify AP bits
        *l2_desc_ptr &= ~(0x3 << 4);  // Clear AP bits
        if ((new_prot & VM_PROT_WRITE) != 0) {
            *l2_desc_ptr |= (0x3 << 4);  // Full access
        } else {
            *l2_desc_ptr |= (0x1 << 4);  // Read-only
        }
        
        // Modify XN bit
        *l2_desc_ptr &= ~1;  // Clear XN
        if ((new_prot & VM_PROT_EXEC) == 0) {
            *l2_desc_ptr |= 1;  // Set XN
        }
        
        found = true;
    }
    
    if (found) {
        arch_mmu_flush_tlb_va(va);
    }
    
    return found;
}

/**
 * @brief Enable the MMU (stub for now).
 */
void arch_mmu_enable(addrspace_t* as) {
    (void)as;  // TODO: implement actual MMU enablement
    // TODO: Implement actual MMU enablement
    // This would involve:
    // - Writing TTBR0 with as->ttbr0_pa
    // - Setting DACR (domain access control)
    // - Setting SCTLR.M bit to enable MMU
    // - ISB, DSB barriers
}

/**
 * @brief Disable the MMU (stub).
 */
void arch_mmu_disable(void) {
    // TODO: Implement MMU disabling
}

/**
 * @brief Switch to another address space (context switch).
 */
void arch_mmu_switch(addrspace_t* as) {
    if (!as) return;
    // TODO: Write TTBR0 with as->ttbr0_pa and flush TLB
}

/**
 * @brief Flush entire TLB (stub).
 */
void arch_mmu_flush_tlb(void) {
    // TODO: Implement full TLB invalidation
}

/**
 * @brief Flush single TLB entry (stub).
 */
void arch_mmu_flush_tlb_va(uintptr_t va) {
    (void)va;  // TODO: implement single-entry TLB invalidation
    // TODO: Implement single-entry TLB invalidation
}

/**
 * @brief Issue memory barriers (stub).
 */
void arch_mmu_barrier(void) {
    // TODO: Implement ISB/DSB barriers
}
