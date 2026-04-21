#include "vmm.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include "arch/arm/mmu/mmu.h"
#include "arch/arm/include/symbols.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <mem.h>
#include "core/panic.h"
#include "kernel/layout.h"
#include "asid.h"

// Track kernel and current address spaces
static addrspace_t* g_kernel_as = NULL;
static addrspace_t* g_current_addrspace = NULL;
static bool g_mmu_enabled = false;
extern phys_region_t phys_region;
extern kernel_layout_t kernel_layout;

#define LOG_FMT(fmt) "(vmm) " fmt
#include "core/log.h"


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

addrspace_t* as_create(addrspace_type_t type) {
    addrspace_t* as = kmalloc(sizeof(addrspace_t));
    if (!as) {
        return NULL;
    }
    as->asid = 0;

    if (type == ADDRSPACE_USER) {
        as->ttbr0_pa = arch_mmu_create_user_tables();
    } else {
        as->ttbr0_pa = arch_mmu_create_tables();  // full 16KB for kernel
    }

    if (as->ttbr0_pa == 0) {
        kfree(as);
        return NULL;
    }

    if (type == ADDRSPACE_USER) {
        as->asid = asid_alloc();
    }

    if (!vm_region_vec_init(&as->regions)) {
        if (as->asid != 0) {
            arch_mmu_flush_tlb_asid(as->asid);
            asid_free(as->asid);
        }
        arch_mmu_free_tables(as->ttbr0_pa, type);
        kfree(as);
        return NULL;
    }

    as->type = type;
    return as;
}

void vmm_lockdown_kernel_sections(void) {
    uint32_t *l1 = (uint32_t *)PA_TO_VA(g_kernel_as->ttbr0_pa);
    uint32_t patched_sections = 0;
    uint32_t patched_pages = 0;

    for (size_t i = 0; i < 4096; i++) {
        uint32_t entry = l1[i];

        // Section descriptor (bits[1:0] == 0b10)
        if ((entry & 0x3) == 0x2) {
            // Clear AP[11:10], set to 0b01 (kernel only)
            entry &= ~(0x3u << 10);
            entry |=  (0x1u << 10);
            l1[i] = entry;
            patched_sections++;
            continue;
        }

        // Coarse page table (bits[1:0] == 0b01)
        if ((entry & 0x3) == 0x1) {
            uint32_t l2_pa = entry & 0xFFFFFC00u;
            uint32_t *l2 = (uint32_t *)PA_TO_VA(l2_pa);

            for (size_t j = 0; j < 256; j++) {
                uint32_t pte = l2[j];

                // Small page descriptor has bits[1:0] == 0b10
                if ((pte & 0x3) != 0x2) {
                    continue;
                }

                // Small page AP bits are [5:4]. Force kernel-only AP=01.
                pte &= ~(0x3u << 4);
                pte |=  (0x1u << 4);
                l2[j] = pte;
                patched_pages++;
            }
        }
    }
    
    // Flush TLB so old permissions are gone
    __asm__ volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(0));
    __asm__ volatile("dsb");
    __asm__ volatile("isb");

    KINFO("Lockdown: patched %u sections and %u L2 pages", patched_sections, patched_pages);
}

void as_destroy(addrspace_t* as) {
    if (!as) return;
    if (as == g_current_addrspace) {
        panic("Attempted to destroy active addrspace");
        __builtin_unreachable();
    }
    
    if (as->asid != 0) {
        // Prevent stale translations from surviving ASID reuse.
        arch_mmu_flush_tlb_asid(as->asid);
        asid_free(as->asid);
    }

    for (uint32_t i = 0; i < as->regions.len; i++) {
        vm_region_t *r = vm_region_vec_get(&as->regions, i);
        vmm_unmap_range(as, r->vaddr_start, r->size);
    }
    
    
    // Free page tables
    arch_mmu_free_tables(as->ttbr0_pa, as->type);
    
    // Destroy regions vector
    vm_region_vec_destroy(&as->regions);

    // Free address space struct
    kfree(as);
}

bool vmm_add_region(addrspace_t *as, const vm_region_t *region) {
    if (!as || !region || region->size == 0) return false;

    uintptr_t new_start = region->vaddr_start;
    uintptr_t new_end   = new_start + region->size;

    // overlap check
    for (uint32_t i = 0; i < as->regions.len; i++) {
        vm_region_t *r = vm_region_vec_get(&as->regions, i);
        if (!(new_end <= r->vaddr_start || new_start >= r->vaddr_start + r->size))
            return false;
    }

    return vm_region_vec_push(&as->regions, region);
}

bool vmm_remove_region(addrspace_t *as, uintptr_t vaddr, size_t size) {
    if (!as || size == 0) return false;

    uint32_t idx = as->regions.len;
    for (uint32_t i = 0; i < as->regions.len; i++) {
        vm_region_t *r = vm_region_vec_get(&as->regions, i);
        if (!r) continue;
        if (r->vaddr_start == vaddr && r->size == size) { idx = i; break; }
    }
    if (idx == as->regions.len) return false;

    vmm_unmap_range(as, vaddr, size);

    // shift down 
    for (uint32_t i = idx; i + 1 < as->regions.len; i++)
        as->regions.data[i] = as->regions.data[i + 1];
    as->regions.len--;

    return true;
}

bool vmm_build_page_tables(addrspace_t* as) {
    if (!as) return false;

    for (uint32_t i = 0; i < as->regions.len; i++) {
        vm_region_t *r = vm_region_vec_get(&as->regions, i);
        if (r->flags & VM_FLAG_GUARD) continue;
        if (!vmm_map_range(as, r->vaddr_start, r->paddr_start, r->size,
                        r->prot, r->memtype, r->owner, r->flags))
            return false;
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
            __builtin_unreachable();
        }

        // early_l1 is in .bss.boot, linked at physical addresses
        // The symbol value IS the physical address
        g_kernel_as->ttbr0_pa = (uintptr_t)early_l1;
        vm_region_vec_init(&g_kernel_as->regions);
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

        //KDEBUG("VMM: Bootstrap complete (adopted early_l1)");
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

    for (uint32_t i = 0; i < g_kernel_as->regions.len; i++) {
        if (g_kernel_as->regions.data[i].vaddr_start == map_pa_start) {
            for (uint32_t j = i; j + 1 < g_kernel_as->regions.len; j++)
                g_kernel_as->regions.data[j] = g_kernel_as->regions.data[j + 1];
            g_kernel_as->regions.len--;
            break;
        }
    }


    //KDEBUG("VMM: Identity mapping removed, running pure higher-half");
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

    // check overflow
    if (va > UINTPTR_MAX - size) return false;

    if (as->type == ADDRSPACE_USER) {
        // For user address spaces, enforce canonical user VA range [0, USER_VA_TOP).
        // end is exclusive, so end == USER_VA_TOP is valid.
        if (va >= USER_VA_TOP || va + size > USER_VA_TOP) {
            return false;
        }
    }

    (void)owner;
    (void)flags;

    // Delegate to arch layer (handles ownership and flags at the architecture level)
    return arch_mmu_map(as, va, pa, size, prot, memtype);
}

bool vmm_unmap_range(addrspace_t* as, uintptr_t va, size_t size) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;    // ← page granularity
    if ((size % PAGE_SIZE) != 0) return false;  // ← page granularity

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
