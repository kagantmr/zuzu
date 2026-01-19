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

addrspace_t* addrspace_create(addrspace_type_t type) {
    addrspace_t* addrspace = kmalloc(sizeof(addrspace_t));
    if (!addrspace) {
        return NULL;
    }
    addrspace->ttbr0_pa = arch_mmu_create_tables();
    if (addrspace->ttbr0_pa == 0) {
        kfree(addrspace);
        return NULL;
    }
    addrspace->regions = NULL;
    addrspace->region_count = 0;
    addrspace->type = type;
    return addrspace;
}

void addrspace_destroy(addrspace_t* as) {
    if (!as) {
        return;
    }
    
    // CRITICAL SAFETY REQUIREMENT:
    // The address space being destroyed must NOT be currently active (TTBR0).
    // If it is active, unmapping and freeing will corrupt the running kernel.
    if (as == g_current_addrspace) {
        KPANIC("Cannot destroy active addrspace");
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
    arch_mmu_free_tables(as->ttbr0_pa);
    
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

void vmm_bootstrap(void) {
    if (!g_kernel_as) {
        g_kernel_as = addrspace_create(ADDRSPACE_KERNEL);
        if (!g_kernel_as) {
            // panic or handle error
            return;
        }

        uintptr_t ram_base = phys_region.start;
        size_t ram_size = phys_region.end - phys_region.start; 

        uintptr_t map_start = ram_base & ~(0x100000 - 1);
        uintptr_t map_end = (ram_base + ram_size + 0x100000 - 1) & ~(0x100000 - 1);
        size_t map_size = map_end - map_start;

        vm_region_t ram_region = {
            .vaddr_start = map_start,
            .paddr_start = map_start,
            .size = map_size,
            .prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_SHARED,
            .flags = VM_FLAG_GLOBAL | VM_FLAG_PINNED,
        };
        if (!vmm_add_region(g_kernel_as, &ram_region)) {
            return;
        }

        vm_region_t uart_region = {
            .vaddr_start = 0x1C000000,
            .paddr_start = 0x1C000000,
            .size = 0x100000,
            .prot = VM_PROT_READ | VM_PROT_WRITE,
            .memtype = VM_MEM_DEVICE,
            .owner = VM_OWNER_NONE,
            .flags = VM_FLAG_GLOBAL | VM_FLAG_PINNED,
        };
        if (!vmm_add_region(g_kernel_as, &uart_region)) {
            return;
        }

        if (!vmm_build_page_tables(g_kernel_as)) {
            return;
        }

        vmm_activate(g_kernel_as);
    }
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
    if ((va % 0x100000) != 0) return false;
    if ((pa % 0x100000) != 0) return false;
    if ((size % 0x100000) != 0) return false;

    (void)owner;  // ownership tracked at higher level; arch layer doesn't need it
    (void)flags;  // flags handled at higher level for now

    // Delegate to arch layer (handles ownership and flags at the architecture level)
    // Note: arch_mmu_map takes a range, not individual pages
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