#include "vmm.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm.h"
#include <arch/mmu.h>
#include <arch/symbols.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "core/panic.h"
#include "kernel/layout.h"
#include <arch/asid.h>
#include <zuzu/types.h>
#include <stdlib.h>

// Track kernel and current address spaces
static AddressSpace* g_kernel_as = NULL;
static AddressSpace* g_current_addrspace = NULL;
static bool g_mmu_enabled = false;
extern kernel_layout_t kernel_layout;
extern uint32_t early_l1[]; 


#define LOG_FMT(fmt) "(vmm) " fmt
#include "core/log.h"

static int region_contains_va(const void *key, const void *elem)
{
    uintptr_t va = *(const uintptr_t *)key;
    const VirtMemRegion *r = (const VirtMemRegion *)elem;
    if (va < r->vaddr_start)       return -1;
    if (va - r->vaddr_start >= r->size) return  1;
    return 0;
}

static int region_cmp_start(const void *key, const void *elem)
{
    uintptr_t va = *(const uintptr_t *)key;
    const VirtMemRegion *r = (const VirtMemRegion *)elem;
    if (va < r->vaddr_start) return -1;
    if (va > r->vaddr_start) return  1;
    return 0;
}

AddressSpace* VmmGetKernelAddrspace(void) {
    return g_kernel_as;
}

// Bitmap: 256 bits = 8 x uint32_t
static uint32_t ioremap_bitmap[8];  // Bit N = slot N allocated

/* IOREMAP_MAX_SLOT is defined in vmm.h as pure text substitution (SECTION_SIZE
 * isn't visible there yet, see the comment at its definition); this is the
 * first point in this TU where both arch/mmu.h (SECTION_SIZE) and vmm.h
 * (KSTACK_REGION_BASE, IOREMAP_BASE, IOREMAP_SLOTS) are in scope together. */
_Static_assert(IOREMAP_MAX_SLOT <= IOREMAP_SLOTS,
	       "kstack region base falls outside the ioremap window");

typedef struct {
    VirtAddr va;       // Base VA (0 = unused entry)
    PhysAddr pa;       // Physical address  
    uint32_t sections;  // Number of 1MB sections
} ioremap_entry_t;
static ioremap_entry_t ioremap_table[IOREMAP_MAX_ENTRIES];

static VirtMemRegion *vmm_find_region(AddressSpace *as, uintptr_t va)
{
    if (!as || as->regions.len == 0)
        return NULL;
    return bsearch(&va, as->regions.data, as->regions.len,
                   sizeof(VirtMemRegion), region_contains_va);
}

bool VmmPageFaultHandle(AddressSpace *restrict as, VirtMemRegion *restrict r, uintptr_t page_va)
{
    if (!as || !r)
        return false;

    if (arch_mmu_translate(as->pt_root_physaddr, page_va) != 0)
        return true;

    if (r->memtype == VM_MEM_DEVICE)
        return false;

    uintptr_t new_pa = 0;
    bool allocated_new = false;

    if (r->owner == VM_OWNER_SHARED && r->backing) {
        ShmCap *shm = (ShmCap *)r->backing;
        if (page_va < r->vaddr_start)
            return false;

        size_t page_index = (size_t)((page_va - r->vaddr_start) / PAGE_SIZE);
        if (page_index >= shm->page_count)
            return false;

        new_pa = shm->page_addrs[page_index];
        if (new_pa == 0) {
            new_pa = PmmAllocFrame();
            if (new_pa == 0)
                return false;
            memset((void *)PA_TO_VA(new_pa), 0, PAGE_SIZE);
            shm->page_addrs[page_index] = new_pa;
            allocated_new = true;
        }
    } else if (r->owner == VM_OWNER_ANON) {
        new_pa = PmmAllocFrame();
        if (new_pa == 0)
            return false;
        memset((void *)PA_TO_VA(new_pa), 0, PAGE_SIZE);
        allocated_new = true;
    } else {
        return false;
    }

    if (!VmmMapRange(as, page_va, new_pa, PAGE_SIZE,
                       r->prot, r->memtype, r->owner, r->flags)) {
        if (allocated_new) {
            if (r->owner == VM_OWNER_SHARED && r->backing) {
                ShmCap *shm = (ShmCap *)r->backing;
                size_t page_index = (size_t)((page_va - r->vaddr_start) / PAGE_SIZE);
                if (page_index < shm->page_count && shm->page_addrs[page_index] == new_pa)
                    shm->page_addrs[page_index] = 0;
            }
            PmmFreeFrame(new_pa);
        }
        return false;
    }

    arch_mmu_flush_tlb_va(page_va);
    arch_mmu_barrier();
    return true;
}


AddressSpace* AddrspaceCreate(AsType type) {
    AddressSpace* as = kmalloc(sizeof(AddressSpace));
    if (!as) {
        return NULL;
    }
    as->asid_token = (asid_token_t){0};

    as->pt_root_physaddr = arch_mmu_create_tables(type);

    if (as->pt_root_physaddr == 0) {
        kfree(as);
        return NULL;
    }

    if (type == ADDRSPACE_USER) {
        as->asid_token = asid_alloc();
        if (as->asid_token.asid == 0) {
            arch_mmu_free_tables(as->pt_root_physaddr, type);
            kfree(as);
            return NULL;
        }
    }

    if (!vm_region_vec_init(&as->regions)) {
        if (as->asid_token.asid != 0) {
            arch_mmu_flush_tlb_asid(as->asid_token.asid);
            asid_free(as->asid_token);
        }
        arch_mmu_free_tables(as->pt_root_physaddr, type);
        kfree(as);
        return NULL;
    }

    as->type = type;
    return as;
}

void VmmLockdownKernelMapping(void) {
    VirtAddr *l1 = (VirtAddr *)PA_TO_VA(g_kernel_as->pt_root_physaddr);
 
    size_t start_idx = kernel_layout.kernel_start_va >> 20;
    size_t end_idx   = (kernel_layout.kernel_end_va + (1 << 20) - 1) >> 20;

    for (size_t i = start_idx; i < end_idx; i++) {
        uint32_t entry = l1[i];

        // Section descriptor (bits[1:0] == 0b10)
        if ((entry & 0x3) == 0x2) {
            // Clear AP[11:10], set to 0b01 (kernel only)
            entry &= ~(0x3u << 10);
            entry |=  (0x1u << 10);
            l1[i] = entry;
            continue;
        }

        // Coarse page table (bits[1:0] == 0b01)
        if ((entry & 0x3) == 0x1) {
            PhysAddr l2_pa = entry & 0xFFFFFC00u;
            VirtAddr *l2 = (VirtAddr *)PA_TO_VA(l2_pa);

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
            }
        }
    }
    
    arch_mmu_barrier();

    // Flush TLB so old permissions are gone
    arch_mmu_flush_tlb();

    arch_mmu_barrier();
}

void AddrspaceDestroy(AddressSpace* as) {
    if (!as) return;
    if (as == g_current_addrspace) {
        panic("Attempted to destroy active addrspace %p (asid=%u)",
              (void *)as, as->asid_token.asid);
        __builtin_unreachable();
    }
    
    if (as->asid_token.asid != 0) {
        // Prevent stale translations from surviving ASID reuse.
        arch_mmu_flush_tlb_asid(as->asid_token.asid);
        asid_free(as->asid_token);
    }

    for (uint32_t i = 0; i < as->regions.len; i++) {
        VirtMemRegion *r = vm_region_vec_get(&as->regions, i);
        VmmUnmapRange(as, r->vaddr_start, r->size);
    }
    
    
    // Free page tables
    arch_mmu_free_tables(as->pt_root_physaddr, as->type);
    
    // Destroy regions vector
    vm_region_vec_destroy(&as->regions);

    // Free address space struct
    kfree(as);
}

bool VmmAddRegion(AddressSpace *restrict as, const VirtMemRegion *restrict region) {
    if (!as || !region || region->size == 0) return false;

    VirtAddr new_start = region->vaddr_start;
    VirtAddr new_end   = new_start + region->size;

    uint32_t lo = 0, hi = as->regions.len;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (as->regions.data[mid].vaddr_start <= new_start)
            lo = mid + 1;
        else
            hi = mid;
    }
    uint32_t ins = lo;

    if (ins > 0) {
        VirtMemRegion *left = &as->regions.data[ins - 1];
        if (left->vaddr_start + left->size > new_start)
            return false;
    }
    if (ins < as->regions.len) {
        if (new_end > as->regions.data[ins].vaddr_start)
            return false;
    }

    if (as->regions.len >= as->regions.cap) {
        if (vm_region_vec_grow(&as->regions) < 0)
            return false;
    }

    memmove(&as->regions.data[ins + 1], &as->regions.data[ins],
            (as->regions.len - ins) * sizeof(VirtMemRegion));
    as->regions.data[ins] = *region;
    as->regions.len++;
    return true;
}

bool VmmRemoveRegion(AddressSpace *as, uintptr_t vaddr, size_t size) {
    if (!as || size == 0) return false;

    VirtMemRegion *r = bsearch(&vaddr, as->regions.data, as->regions.len,
                              sizeof(VirtMemRegion), region_cmp_start);
    if (!r || r->size != size)
        return false;

    VmmUnmapRange(as, vaddr, size);

    uint32_t idx = (uint32_t)(r - as->regions.data);
    memmove(r, r + 1, (as->regions.len - idx - 1) * sizeof(VirtMemRegion));
    as->regions.len--;
    return true;
}

bool VmmBuildPts(AddressSpace* as) {
    if (!as) return false;

    for (uint32_t i = 0; i < as->regions.len; i++) {
        VirtMemRegion *r = vm_region_vec_get(&as->regions, i);
        if (r->flags & VM_FLAG_GUARD) continue;
        if (!VmmMapRange(as, r->vaddr_start, r->paddr_start, r->size,
                        r->prot, r->memtype, r->owner, r->flags))
            return false;
    }
    return true;
}


void vmm_bootstrap(void) {
    if (!g_kernel_as) {
        g_kernel_as = kmalloc(sizeof(AddressSpace));
        if (!g_kernel_as) {
            panic("Failed to create kernel address space");
            __builtin_unreachable();
        }

        // allocate a PMM-backed L1 and copy early_l1 into it
        uintptr_t new_l1_pa = arch_mmu_create_tables(ADDRSPACE_KERNEL);
        if (!new_l1_pa) {
            panic("Failed to allocate kernel L1 from PMM");
        }
        const size_t l1_bytes = 16 * 1024;
        void *new_l1_va = (void *)PA_TO_VA(new_l1_pa);
        void *early_l1_va = (void *)PA_TO_VA((uintptr_t)early_l1);
        memcpy(new_l1_va, early_l1_va, l1_bytes); // copy early table

        // assign and switch TTBR to the new table
        g_kernel_as->pt_root_physaddr = new_l1_pa;

        // arch_mmu_switch installs the new TTBR
        arch_mmu_switch(g_kernel_as);

        vm_region_vec_init(&g_kernel_as->regions);
        g_kernel_as->type = ADDRSPACE_KERNEL;
        g_kernel_as->asid_token = (asid_token_t){0};

        g_mmu_enabled = true;
        g_current_addrspace = g_kernel_as;

        // Record kernel RAM region for bookkeeping
        PhysAddr ram_pa_base = kernel_layout.ram_start;
        size_t ram_size = kernel_layout.ram_end - kernel_layout.ram_start;
        PhysAddr map_pa_start = ram_pa_base & ~(SECTION_SIZE - 1);
        PhysAddr map_pa_end = (ram_pa_base + ram_size + SECTION_SIZE - 1) & ~(SECTION_SIZE - 1);
        size_t map_size = map_pa_end - map_pa_start;

        VirtMemRegion kernel_region = {
            .vaddr_start = PA_TO_VA(map_pa_start),
            .paddr_start = map_pa_start,
            .size = map_size,
            .prot = PROT_READ | PROT_WRITE | PROT_EXEC,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_SHARED,
            .flags = VM_FLAG_GLOBAL | VM_FLAG_PINNED,
        };
        VmmAddRegion(g_kernel_as, &kernel_region);

        // Record identity mapping so vmm_remove_identity_mapping can find it
        VirtMemRegion identity_region = {
            .vaddr_start = map_pa_start,
            .paddr_start = map_pa_start,
            .size = map_size,
            .prot = PROT_READ | PROT_WRITE | PROT_EXEC,
            .memtype = VM_MEM_NORMAL,
            .owner = VM_OWNER_NONE,
            .flags = VM_FLAG_NONE,
        };
        VmmAddRegion(g_kernel_as, &identity_region);

        //KDEBUG("VMM: Bootstrap complete (adopted early_l1)");
    }
}

void VmmRemoveIdentityMapping(void) {
    if (!g_kernel_as) {
        return;
    }

    PhysAddr ram_pa_base = kernel_layout.ram_start;
    size_t ram_size = kernel_layout.ram_end - kernel_layout.ram_start;
    PhysAddr map_pa_start = ram_pa_base & ~(SECTION_SIZE - 1);
    PhysAddr map_pa_end = (ram_pa_base + ram_size + SECTION_SIZE - 1) & ~(SECTION_SIZE - 1);
    size_t map_size = map_pa_end - map_pa_start;

    PhysAddr cur_sp = 0;
    __asm__ volatile("mov %0, sp" : "=r"(cur_sp));

    if (cur_sp < KERNEL_VA_BASE) {
        uint32_t offset = KERNEL_VA_OFFSET;

        arch_relocate_stacks(offset);
    }

    VmmUnmapRange(g_kernel_as, map_pa_start, map_size);
    KDEBUG("identity unmapped, pruning region");

    VirtMemRegion *r = bsearch(&map_pa_start, g_kernel_as->regions.data,
                              g_kernel_as->regions.len,
                              sizeof(VirtMemRegion), region_cmp_start);
    if (r) {
        uint32_t idx = (uint32_t)(r - g_kernel_as->regions.data);
        memmove(r, r + 1,
                (g_kernel_as->regions.len - idx - 1) * sizeof(VirtMemRegion));
        g_kernel_as->regions.len--;
    }

    KDEBUG("identity mapping removed, running pure higher-half");
}

void VmmActivateAddrspace(AddressSpace* as) {
    if (!as) return;

    if (!g_mmu_enabled) {
        arch_mmu_enable(as);
        g_mmu_enabled = true;
    } else {
        arch_mmu_switch(as);
    }

    g_current_addrspace = as;
}

bool VmmMapRange(AddressSpace* as, VirtAddr va, PhysAddr pa, size_t size,
                   MemProt prot, VirtMemType memtype, VirtMemOwner owner, VirtMemFlags flags) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % 0x1000) != 0) return false;
    if ((pa % 0x1000) != 0) return false;

    // check overflow
    if (va > UINTPTR_MAX - size) return false;

    if (as->type == ADDRSPACE_USER && (prot & PROT_WRITE) && (prot & PROT_EXEC))
        return false;

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

bool VmmUnmapRange(AddressSpace* as, VirtAddr va, size_t size) {
    if (!as) return false;
    if (size == 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;    // page granularity
    if ((size % PAGE_SIZE) != 0) return false;  // page granularity

    return arch_mmu_unmap(as, va, size);
}

bool VmmProtectPage(AddressSpace *as, VirtAddr va, size_t size, MemProt new_prot)
{
    if (!as || size == 0) return false;

    VirtMemRegion *r = vmm_find_region(as, va);
    if (!r) return false;                          /* no region → refuse */
    if (va + size > r->vaddr_start + r->size)      /* must not span regions */
        return false;
    if (r->memtype == VM_MEM_DEVICE && (new_prot & PROT_EXEC))
        return false;                              /* no executable MMIO */
    if (r->flags & VM_FLAG_PINNED)                 /* tcb_page/syspage */
        return false;

    if (!arch_mmu_protect(as, va, size, new_prot))
        return false;

    r->prot = new_prot;                            /* keep region truth in sync */
    return true;
}

bool VmmMapUserPage(AddressSpace* as, PhysAddr pa, VirtAddr va, MemProt prot) {
    if (!as) return false;
    if (as->type != ADDRSPACE_USER) return false;
    if ((pa % PAGE_SIZE) != 0) return false;
    if ((va % PAGE_SIZE) != 0) return false;

    return VmmMapRange(as, va, pa, PAGE_SIZE, prot | VM_PROT_USER,
                         VM_MEM_NORMAL, VM_OWNER_SHARED, VM_FLAG_NONE);
}

// Find N contiguous free bits in bitmap, return starting index or -1.
// Bounded to IOREMAP_MAX_SLOT, not IOREMAP_SLOTS: slots beyond that would
// land on the kstack region (see IOREMAP_MAX_SLOT in vmm.h).
static int bitmap_find_free(uint32_t n) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < IOREMAP_MAX_SLOT; i++) {
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

bool VmmCheckUserFault(AddressSpace *as, VirtAddr va, size_t len, bool write) {
    if (!as)
        return false;
    if (len == 0)
        return true;
    if (va > UINTPTR_MAX - len)
        return false;

    const uintptr_t end = va + len;
    if (as->type == ADDRSPACE_USER && (va >= USER_VA_TOP || end > USER_VA_TOP))
        return false;

    uintptr_t page_va = align_down(va, PAGE_SIZE);
    const uintptr_t end_va = align_up(end, PAGE_SIZE);

    while (page_va < end_va) {
        if (arch_mmu_translate(as->pt_root_physaddr, page_va) != 0) {
            // Already mapped — nothing to do
            page_va += PAGE_SIZE;
            continue;
        }
        VirtMemRegion *r = vmm_find_region(as, page_va);
        if (!r)
            return false;
        if (r->flags & VM_FLAG_GUARD)
            return false;
        if (!(r->prot & PROT_READ))
            return false;
        if (write && !(r->prot & PROT_WRITE))
            return false;

        if (!VmmPageFaultHandle(as, r, page_va))
            return false;

        page_va += PAGE_SIZE;
    }

    return true;
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
static ioremap_entry_t* ioremap_find(VirtAddr va) {
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

void* IoRemap(PhysAddr phys, size_t size) {
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
    /* Belt-and-suspenders: bitmap_find_free is already bounded to
     * IOREMAP_MAX_SLOT, but a mapping must never be allowed to land on the
     * kstack region even if that bound is ever loosened by mistake. */
    if ((uint32_t)slot + sections_needed > IOREMAP_MAX_SLOT) {
        return NULL;
    }

    uintptr_t va = IOREMAP_BASE + (slot * SECTION_SIZE);

    if (!VmmMapRange(g_kernel_as, va, phys_aligned, aligned_size, 
                       PROT_READ | PROT_WRITE,
                       VM_MEM_DEVICE, VM_OWNER_NONE,
                       VM_FLAG_PINNED | VM_FLAG_GLOBAL)) {
        return NULL;
    }

    bitmap_alloc(slot, sections_needed);

    ioremap_entry_t* entry = ioremap_alloc_entry();
    if (!entry) {
        VmmUnmapRange(g_kernel_as, va, aligned_size);
        bitmap_free(slot, sections_needed);
        return NULL;
    }
    entry->va = va;
    entry->pa = phys_aligned; 
    entry->sections = sections_needed;

    return (void*)(va + offset);
}

void IoUnmap(void* va) {
    if (!va) {
        return;
    }

    uintptr_t base_va = align_down((uintptr_t)va, SECTION_SIZE);
    ioremap_entry_t* entry = ioremap_find(base_va);
    if (!entry) {
        return;
    }

    size_t size = entry->sections * SECTION_SIZE;
    VmmUnmapRange(g_kernel_as, entry->va, size);

    uint32_t slot_start = (entry->va - IOREMAP_BASE) / SECTION_SIZE;
    bitmap_free(slot_start, entry->sections);

    entry->va = 0;
    entry->pa = 0;
    entry->sections = 0;
}
