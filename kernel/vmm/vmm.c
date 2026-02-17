#include "vmm.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"
#include "arch/arm/include/symbols.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lib/string.h"
#include "core/log.h"
#include "lib/mem.h"
#include "kernel/layout.h"

// Track kernel and current address spaces
static addrspace_t* g_kernel_as = NULL;
static addrspace_t* g_current_addrspace = NULL;
static bool g_mmu_enabled = false;
extern phys_region_t phys_region;
extern kernel_layout_t kernel_layout;

addrspace_t* vmm_get_kernel_as(void) {
    return g_kernel_as;
}

// Bitmap: 256 bits = 8 x uint32_t
static uint32_t ioremap_bitmap[8];  // Bit N = slot N allocated

typedef struct {
    uintptr_t va;       // Base VA (0 = unused entry)
    uintptr_t pa;       // Physical address  
    uint32_t sections;  // Number of 1MB sections
} ioremap_entry_t;
static ioremap_entry_t ioremap_table[IOREMAP_MAX_ENTRIES];

addrspace_t* addrspace_create(addrspace_type_t type) {
    addrspace_t* addrspace = kmalloc(sizeof(addrspace_t));
    if (!addrspace) {
        return NULL;
    }
    if (type == ADDRSPACE_USER) {
        addrspace->ttbr0_pa = arch_mmu_create_user_tables();
    } else {
        addrspace->ttbr0_pa = arch_mmu_create_tables();  // full 16KB for kernel
    }
    if (addrspace->ttbr0_pa == 0) {
        kfree(addrspace);
        return NULL;
    }
    addrspace->regions = NULL;
    addrspace->region_count = 0;
    addrspace->type = type;
    return addrspace;
}

void vmm_lockdown_kernel_sections(void) {
    uint32_t *l1 = (uint32_t *)PA_TO_VA(g_kernel_as->ttbr0_pa);
    uint32_t patched = 0;

    for (size_t i = 0; i < 4096; i++) {
        uint32_t entry = l1[i];
        
        // Check if it's a section descriptor (bits[1:0] == 0b10)
        if ((entry & 0x3) != 0x2) continue;
        
        // Clear AP[11:10], set to 0b01 (kernel only)
        entry &= ~(0x3 << 10);   // clear AP[1:0]
        entry |=  (0x1 << 10);   // set AP = 0b01
        
        l1[i] = entry;
        patched++;
    }
    
    // Flush TLB so old permissions are gone
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));
    __asm__ volatile("dsb");
    __asm__ volatile("isb");

    KINFO("Lockdown: patched %u section entries", patched);
}

void addrspace_destroy(addrspace_t* as) {
    if (!as) {
        return;
    }
    
    // CRITICAL SAFETY REQUIREMENT:
    // The address space being destroyed must NOT be currently active (TTBR0).
    // If it is active, unmapping and freeing will corrupt the running kernel.
    if (as == g_current_addrspace) {
        panic("Attempted to destroy active addrspace");
        return;  // For now, silently fail to prevent corruption
    }
    
    // Phase-2 bring-up policy (section mappings):
    // Do NOT walk page tables to translate VA->PA or free backing pages here.
    // The current arch MMU layer is section-only; translate() is coarse and not page-aligned.
    // We only unmap ranges (best-effort) and then free the page table structures.
    for (size_t i = 0; i < as->region_count; i++) {
        vm_region_t* region = &as->regions[i];
        (void)vmm_unmap_range(as, region->vaddr_start, region->size);
    }
    
    // Free page tables
    arch_mmu_free_tables(as->ttbr0_pa, as->type);
    
    // Free regions array
    if (as->regions) {
        kfree(as->regions);
    }
    // Free address space struct
    kfree(as);
}

bool vmm_add_region(addrspace_t* as, const vm_region_t* region) {
    if (!as || !region) return false;
    if (region->size == 0) return false;
    if ((region->vaddr_start % PAGE_SIZE) != 0) return false;
    if ((region->size % PAGE_SIZE) != 0) return false;

    uintptr_t new_start = region->vaddr_start;
    uintptr_t new_end = new_start + region->size;

    // Check for overlap with existing regions
    for (size_t i = 0; i < as->region_count; i++) {
        vm_region_t* r = &as->regions[i];
        uintptr_t r_start = r->vaddr_start;
        uintptr_t r_end = r_start + r->size;
        if (!(new_end <= r_start || new_start >= r_end)) {
            // Overlap detected
            return false;
        }
    }

    // Insert region sorted by vaddr_start
    size_t pos = 0;
    while (pos < as->region_count && as->regions[pos].vaddr_start < new_start) {
        pos++;
    }

    vm_region_t* new_regions = kmalloc(sizeof(vm_region_t) * (as->region_count + 1));
    if (!new_regions) return false;

    // Copy regions before pos
    if (pos > 0) {
        memcpy(new_regions, as->regions, sizeof(vm_region_t) * pos);
    }
    // Insert new region
    new_regions[pos] = *region;
    // Copy regions after pos
    if (pos < as->region_count) {
        memcpy(new_regions + pos + 1, as->regions + pos, sizeof(vm_region_t) * (as->region_count - pos));
    }

    if (as->regions) {
        kfree(as->regions);
    }
    as->regions = new_regions;
    as->region_count++;

    return true;
}

bool vmm_remove_region(addrspace_t* as, uintptr_t vaddr, size_t size) {
    if (!as) return false;
    if (size == 0) return false;
    if ((vaddr % PAGE_SIZE) != 0) return false;
    if ((size % PAGE_SIZE) != 0) return false;

    size_t idx = as->region_count;
    for (size_t i = 0; i < as->region_count; i++) {
        vm_region_t* r = &as->regions[i];
        if (r->vaddr_start == vaddr && r->size == size) {
            idx = i;
            break;
        }
    }
    if (idx == as->region_count) {
        // Not found
        return false;
    }

    // Unmap the region (do not free pages)
    if (!vmm_unmap_range(as, vaddr, size)) {
        return false;
    }

    // Remove region by shifting tail down
    for (size_t i = idx; i + 1 < as->region_count; i++) {
        as->regions[i] = as->regions[i + 1];
    }
    as->region_count--;

    if (as->region_count == 0) {
        kfree(as->regions);
        as->regions = NULL;
    } else {
        vm_region_t* new_regions = kmalloc(sizeof(vm_region_t) * as->region_count);
        if (new_regions) {
            memcpy(new_regions, as->regions, sizeof(vm_region_t) * as->region_count);
            kfree(as->regions);
            as->regions = new_regions;
        }
        // If kmalloc failed, keep old array to avoid losing data
    }

    return true;
}

bool vmm_build_page_tables(addrspace_t* as) {
    if (!as) return false;

    for (size_t i = 0; i < as->region_count; i++) {
        vm_region_t* r = &as->regions[i];
        if ((r->flags & VM_FLAG_GUARD) != 0) {
            // Skip guard regions
            continue;
        }
        uintptr_t va = r->vaddr_start;
        uintptr_t pa = r->paddr_start;
        size_t size = r->size;
        if (!vmm_map_range(as, va, pa, size, r->prot, r->memtype, r->owner, r->flags)) {
            return false;
        }
    }
    return true;
}

extern uint32_t early_l1[];  // from early.c, in .bss.boot (physical address)

void vmm_bootstrap(void) {
    if (!g_kernel_as) {
        // Adopt the early boot page table — no new allocation, no page table switch
        g_kernel_as = kmalloc(sizeof(addrspace_t));
        if (!g_kernel_as) {
            panic("Failed to create kernel address space");
            return;
        }

        // early_l1 is in .bss.boot, linked at physical addresses
        // The symbol value IS the physical address
        g_kernel_as->ttbr0_pa = (uintptr_t)early_l1;
        g_kernel_as->regions = NULL;
        g_kernel_as->region_count = 0;
        g_kernel_as->type = ADDRSPACE_KERNEL;
        g_kernel_as->asid = 0;

        // We're already running on early_l1 — no activate needed
        g_mmu_enabled = true;
        g_current_addrspace = g_kernel_as;

        // Record kernel RAM region for bookkeeping
        uintptr_t ram_pa_base = phys_region.start;
        size_t ram_size = phys_region.end - phys_region.start;
        uintptr_t map_pa_start = ram_pa_base & ~(SECTION_SIZE - 1);
        uintptr_t map_pa_end = (ram_pa_base + ram_size + SECTION_SIZE - 1) & ~(SECTION_SIZE - 1);
        size_t map_size = map_pa_end - map_pa_start;

        vm_region_t kernel_region = {
            .vaddr_start = PA_TO_VA(map_pa_start),
            .paddr_start = map_pa_start,
            .size = map_size,
            .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_SHARED,
            .flags = VM_FLAG_GLOBAL | VM_FLAG_PINNED,
        };
        vmm_add_region(g_kernel_as, &kernel_region);

        // Record identity mapping so vmm_remove_identity_mapping can find it
        vm_region_t identity_region = {
            .vaddr_start = map_pa_start,
            .paddr_start = map_pa_start,
            .size = map_size,
            .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_NONE,
            .flags = VM_FLAG_NONE,
        };
        vmm_add_region(g_kernel_as, &identity_region);

        KDEBUG("VMM: Bootstrap complete (adopted early_l1)");
    }
}

void vmm_remove_identity_mapping(void) {
    if (!g_kernel_as) {
        return;
    }

    uintptr_t ram_pa_base = phys_region.start;
    size_t ram_size = phys_region.end - phys_region.start;
    uintptr_t map_pa_start = ram_pa_base & ~(SECTION_SIZE - 1);
    uintptr_t map_pa_end = (ram_pa_base + ram_size + SECTION_SIZE - 1) & ~(SECTION_SIZE - 1);
    size_t map_size = map_pa_end - map_pa_start;

    uintptr_t cur_sp = 0;
    __asm__ volatile("mov %0, sp" : "=r"(cur_sp));

    if (cur_sp < KERNEL_VA_BASE) {
        uint32_t offset = KERNEL_VA_OFFSET;

        __asm__ volatile(
            // Save current mode (should be SVC)
            "mrs    r0, cpsr\n\t"
            "mov    r4, r0\n\t"             // r4 = saved CPSR

            // Disable IRQ/FIQ during mode switches (safety)
            "orr    r0, r0, #0xC0\n\t"      // Set I and F bits
            "msr    cpsr_c, r0\n\t"

            // --- Relocate SVC stack (current mode) ---
            "add    sp, sp, %0\n\t"
            "add    fp, fp, %0\n\t"

            // --- Switch to IRQ mode and relocate ---
            "cps    #0x12\n\t"              // IRQ mode
            "add    sp, sp, %0\n\t"

            // --- Switch to ABT mode and relocate ---
            "cps    #0x17\n\t"              // Abort mode
            "add    sp, sp, %0\n\t"

            // --- Switch to UND mode and relocate ---
            "cps    #0x1B\n\t"              // Undefined mode
            "add    sp, sp, %0\n\t"

            // --- Return to SVC mode ---
            "cps    #0x13\n\t"              // Back to SVC

            // Restore original CPSR (re-enables interrupts if they were enabled)
            "msr    cpsr_c, r4\n\t"

            :
            : "r"(offset)
            : "r0", "r4", "memory"
        );
    }

    vmm_unmap_range(g_kernel_as, map_pa_start, map_size);

    for (size_t i = 0; i < g_kernel_as->region_count; i++) {
        if (g_kernel_as->regions[i].vaddr_start == map_pa_start) {
            for (size_t j = i; j + 1 < g_kernel_as->region_count; j++) {
                g_kernel_as->regions[j] = g_kernel_as->regions[j + 1];
            }
            g_kernel_as->region_count--;
            break;
        }
    }


    KDEBUG("VMM: Identity mapping removed, running pure higher-half");
}

void vmm_activate(addrspace_t* as) {
    if (!as) return;

    if (!g_mmu_enabled) {
        arch_mmu_enable(as);
        g_mmu_enabled = true;
    } else {
        arch_mmu_switch(as);
    }

    g_current_addrspace = as;
}

bool vmm_map_range(addrspace_t* as, uintptr_t va, uintptr_t pa, size_t size,
                   vm_prot_t prot, vm_memtype_t memtype, vm_owner_t owner, vm_flags_t flags) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % 0x1000) != 0) return false;
    if ((pa % 0x1000) != 0) return false;

    (void)owner;  
    (void)flags;  

    // Delegate to arch layer (handles ownership and flags at the architecture level)
    return arch_mmu_map(as, va, pa, size, prot, memtype);
}

bool vmm_unmap_range(addrspace_t* as, uintptr_t va, size_t size) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % 0x100000) != 0) return false;
    if ((size % 0x100000) != 0) return false;

    // Delegate to arch layer (walks page tables, clears entries, invalidates TLB)
    return arch_mmu_unmap(as, va, size);
}

bool vmm_protect_range(addrspace_t* as, uintptr_t va, size_t size, vm_prot_t new_prot) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % 0x100000) != 0) return false;
    if ((size % 0x100000) != 0) return false;

    // Delegate to arch layer (changes permissions on page table entries)
    return arch_mmu_protect(as, va, size, new_prot);
}

uintptr_t kmap_mmio(uintptr_t pa, size_t size) {
    (void )size; // size is unused in section mapping mode
    if (!g_kernel_as) {
        vmm_bootstrap();
        if (!g_kernel_as) {
            return 0;
        }
    }

    uintptr_t section_base = pa & ~(0x100000 - 1);

    if (!vmm_map_range(g_kernel_as, section_base, section_base, 0x100000,
                       VM_PROT_READ | VM_PROT_WRITE, VM_MEM_DEVICE,
                       VM_OWNER_NONE, VM_FLAG_PINNED | VM_FLAG_GLOBAL)) {
        return 0;
    }

    return pa;
}

bool kmap_user_page(addrspace_t* as, uintptr_t pa, uintptr_t va, vm_prot_t prot) {
    if (!as) return false;
    if (as->type != ADDRSPACE_USER) return false;
    if ((pa % PAGE_SIZE) != 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;

    return vmm_map_range(as, va, pa, PAGE_SIZE, prot | VM_PROT_USER,
                         VM_MEM_NORMAL, VM_OWNER_SHARED, VM_FLAG_NONE);
}




// Find N contiguous free bits in bitmap, return starting index or -1
static int bitmap_find_free(uint32_t n) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < IOREMAP_SLOTS; i++) {
        uint32_t word = ioremap_bitmap[i / 32];
        uint32_t bit = 1u << (i % 32);
        if ((word & bit) == 0) {
            count++;
            if (count == n) {
                return i + 1 - n;
            }
        } else {
            count = 0;
        }
    }
    return -1;
}

// Mark bits [start, start+count) as used
static void bitmap_alloc(uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = start + i;
        uint32_t bit = 1u << (idx % 32);
        ioremap_bitmap[idx / 32] |= bit;
    }
}

// Mark bits [start, start+count) as free  
static void bitmap_free(uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = start + i;
        uint32_t bit = 1u << (idx % 32);
        ioremap_bitmap[idx / 32] &= ~bit;
    }
}

// Find ioremap_table entry by VA
static ioremap_entry_t* ioremap_find(uintptr_t va) {
    for (size_t i = 0; i < IOREMAP_MAX_ENTRIES; i++) {
        if (ioremap_table[i].va == va) {
            return &ioremap_table[i];
        }
    }
    return NULL;
}

// Find free slot in ioremap_table
static ioremap_entry_t* ioremap_alloc_entry(void) {
    for (size_t i = 0; i < IOREMAP_MAX_ENTRIES; i++) {
        if (ioremap_table[i].va == 0) {
            return &ioremap_table[i];
        }
    }
    return NULL;
}

void* ioremap(uintptr_t phys, size_t size) {
    if (size == 0) {
        return NULL;
    }

    uintptr_t phys_aligned = align_down(phys, SECTION_SIZE);
    uintptr_t offset = phys - phys_aligned;
    size_t total_size = size + offset;
    size_t aligned_size = align_up(total_size, SECTION_SIZE);
    uint32_t sections_needed = aligned_size / SECTION_SIZE;

    int slot = bitmap_find_free(sections_needed);
    if (slot < 0) {
        return NULL;
    }

    uintptr_t va = IOREMAP_BASE + (slot * SECTION_SIZE);

    if (!vmm_map_range(g_kernel_as, va, phys_aligned, aligned_size, 
                       VM_PROT_READ | VM_PROT_WRITE,
                       VM_MEM_DEVICE, VM_OWNER_NONE,
                       VM_FLAG_PINNED | VM_FLAG_GLOBAL)) {
        return NULL;
    }

    bitmap_alloc(slot, sections_needed);

    ioremap_entry_t* entry = ioremap_alloc_entry();
    if (!entry) {
        vmm_unmap_range(g_kernel_as, va, aligned_size);
        bitmap_free(slot, sections_needed);
        return NULL;
    }
    entry->va = va;
    entry->pa = phys_aligned; 
    entry->sections = sections_needed;

    return (void*)(va + offset);
}

void iounmap(void* va) {
    if (!va) {
        return;
    }

    uintptr_t base_va = align_down((uintptr_t)va, SECTION_SIZE);
    ioremap_entry_t* entry = ioremap_find(base_va);
    if (!entry) {
        return;
    }

    size_t size = entry->sections * SECTION_SIZE;
    vmm_unmap_range(g_kernel_as, entry->va, size);

    uint32_t slot_start = (entry->va - IOREMAP_BASE) / SECTION_SIZE;
    bitmap_free(slot_start, entry->sections);

    entry->va = 0;
    entry->pa = 0;
    entry->sections = 0;
}
