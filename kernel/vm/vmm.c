#include "vmm.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"
#include "arch/arm/include/symbols.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lib/string.h"
#include "lib/mem.h"

// Track kernel and current address spaces
static addrspace_t* g_kernel_as = NULL;
static addrspace_t* g_current_addrspace = NULL;

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
        // panic("Cannot destroy active addrspace");
        return;  // For now, silently fail to prevent corruption
    }
    
    // Ownership-aware cleanup:
    // - For VM_OWNER_ANON regions: walk page tables, translate VA→PA, free each page
    // - For VM_OWNER_SHARED/NONE regions: just unmap, do not free pages
    // This must happen BEFORE destroying page tables so we can still translate VA→PA.
    for (size_t i = 0; i < as->region_count; i++) {
        vm_region_t* region = &as->regions[i];
        
        if (region->owner == VM_OWNER_ANON) {
            // Walk page tables to find actual physical pages for this VA range
            for (uintptr_t va = region->vaddr_start; va < region->vaddr_start + region->size; va += PAGE_SIZE) {
                uintptr_t pa = arch_mmu_translate(as->ttbr0_pa, va);
                if (pa != 0) {
                    // This page was allocated; free it back to PMM
                    free_page(pa);
                }
            }
            // Unmap the region (invalidates TLB if needed)
            vmm_unmap_range(as, region->vaddr_start, region->size);
        } else {
            // VM_OWNER_SHARED or VM_OWNER_NONE: unmap but do not free pages
            vmm_unmap_range(as, region->vaddr_start, region->size);
        }
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
        
        // Add kernel code region (RX, shared - owned by bootloader)
        vm_region_t kernel_code = {
            .vaddr_start = (uintptr_t)_kernel_start,
            .size = (uintptr_t)_kernel_end - (uintptr_t)_kernel_start,
            .prot = VM_PROT_READ | VM_PROT_EXEC,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_SHARED,  // Kernel memory, not freed on destroy
            .paddr_start = (uintptr_t)_kernel_start,  // Identity map for now
            .flags = VM_FLAG_GLOBAL | VM_FLAG_PINNED,
        };
        if (!vmm_add_region(g_kernel_as, &kernel_code)) {
            return;
        }
        
        // Add kernel SVC stack (RW, shared)
        vm_region_t svc_stack = {
            .vaddr_start = (uintptr_t)__svc_stack_base__,
            .size = (uintptr_t)__svc_stack_top__ - (uintptr_t)__svc_stack_base__,
            .prot = VM_PROT_READ | VM_PROT_WRITE,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_SHARED,
            .paddr_start = (uintptr_t)__svc_stack_base__,
            .flags = VM_FLAG_GLOBAL | VM_FLAG_PINNED,
        };
        if (!vmm_add_region(g_kernel_as, &svc_stack)) {
            return;
        }
        
        // Add kernel IRQ stack (RW, shared)
        vm_region_t irq_stack = {
            .vaddr_start = (uintptr_t)__irq_stack_base__,
            .size = (uintptr_t)__irq_stack_top__ - (uintptr_t)__irq_stack_base__,
            .prot = VM_PROT_READ | VM_PROT_WRITE,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_SHARED,
            .paddr_start = (uintptr_t)__irq_stack_base__,
            .flags = VM_FLAG_GLOBAL | VM_FLAG_PINNED,
        };
        if (!vmm_add_region(g_kernel_as, &irq_stack)) {
            return;
        }
        
        // Build page tables from regions
        if (!vmm_build_page_tables(g_kernel_as)) {
            return;
        }
        
        // Activate the kernel address space
        vmm_activate(g_kernel_as);
    }
}

void vmm_activate(addrspace_t* as) {
    if (!as) return;
    arch_mmu_switch(as);
    g_current_addrspace = as;
}

bool vmm_map_range(addrspace_t* as, uintptr_t va, uintptr_t pa, size_t size,
                   vm_prot_t prot, vm_memtype_t memtype, vm_owner_t owner, vm_flags_t flags) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;
    if ((pa % PAGE_SIZE) != 0) return false;
    if ((size % PAGE_SIZE) != 0) return false;

    (void)owner;  // ownership tracked at higher level; arch layer doesn't need it
    (void)flags;  // flags handled at higher level for now

    // Delegate to arch layer (handles ownership and flags at the architecture level)
    // Note: arch_mmu_map takes a range, not individual pages
    return arch_mmu_map(as, va, pa, size, prot, memtype);
}

bool vmm_unmap_range(addrspace_t* as, uintptr_t va, size_t size) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;
    if ((size % PAGE_SIZE) != 0) return false;

    // Delegate to arch layer (walks page tables, clears entries, invalidates TLB)
    return arch_mmu_unmap(as, va, size);
}

bool vmm_protect_range(addrspace_t* as, uintptr_t va, size_t size, vm_prot_t new_prot) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;
    if ((size % PAGE_SIZE) != 0) return false;

    // Delegate to arch layer (changes permissions on page table entries)
    return arch_mmu_protect(as, va, size, new_prot);
}

uintptr_t kmap_mmio(uintptr_t pa, size_t size) {
    static uintptr_t mmio_next = 0;
    if (mmio_next == 0) {
        mmio_next = 0xF0000000;
    }
    if (size == 0) return 0;

    // Align mmio_next to PAGE_SIZE
    if ((mmio_next % PAGE_SIZE) != 0) {
        mmio_next = (mmio_next + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }

    // Round size up to PAGE_SIZE multiple
    size_t size_rounded = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // Map in kernel address space
    if (!vmm_map_range(g_kernel_as, mmio_next, pa, size_rounded,
                       VM_PROT_READ | VM_PROT_WRITE, VM_MEM_DEVICE,
                       VM_OWNER_NONE, VM_FLAG_PINNED | VM_FLAG_GLOBAL)) {
        return 0;
    }

    uintptr_t allocated_va = mmio_next;
    mmio_next += size_rounded;

    return allocated_va;
}

bool kmap_user_page(addrspace_t* as, uintptr_t pa, uintptr_t va, vm_prot_t prot) {
    if (!as) return false;
    if (as->type != ADDRSPACE_USER) return false;
    if ((pa % PAGE_SIZE) != 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;

    return vmm_map_range(as, va, pa, PAGE_SIZE, prot | VM_PROT_USER,
                         VM_MEM_NORMAL, VM_OWNER_SHARED, VM_FLAG_NONE);
}