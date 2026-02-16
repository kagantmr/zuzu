#include "kstack.h"
#include "kernel/mm/pmm.h"
#include "core/assert.h"
#include "kernel/vmm/vmm.h"
#include "arch/arm/mmu/mmu.h"
#include "stdbool.h"

static uint64_t bitmap;
static uintptr_t slot_pa[64];

uintptr_t kstack_alloc(void) {
    for (int i = 0; i < 64; i++) {
        if (!(bitmap & (1ULL << i))) {
            uintptr_t page_pa = pmm_alloc_page();
            if (!page_pa) {
                return 0;
            }
            slot_pa[i] = page_pa;
            uintptr_t slot_va = KSTACK_REGION_BASE + i * 0x2000;

            /* Map the usable stack page */
            bool result = vmm_map_range(vmm_get_kernel_as(), slot_va + 0x1000, page_pa, PAGE_SIZE,
              VM_PROT_READ | VM_PROT_WRITE, VM_MEM_NORMAL, VM_OWNER_ANON, VM_FLAG_NONE);
            kassert(result);

            /* Unmap the guard page (may have been part of a section mapping) */
            arch_mmu_unmap_page(vmm_get_kernel_as(), slot_va);
            arch_mmu_flush_tlb();
            arch_mmu_barrier();

            bitmap |= (1ULL << i);
            return slot_va + 0x2000;
        }
    }
    return 0;
}
void kstack_free(uintptr_t stack_top) {
    int slot = (stack_top - KSTACK_REGION_BASE) / 0x2000 - 1;
    uintptr_t mapped_va = KSTACK_REGION_BASE + slot * 0x2000 + 0x1000;
    vmm_unmap_range(vmm_get_kernel_as(), mapped_va, PAGE_SIZE);
    pmm_free_page(slot_pa[slot]);
    bitmap &= ~(1ULL << slot);
}