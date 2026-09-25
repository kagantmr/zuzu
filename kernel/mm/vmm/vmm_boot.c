/* One-shot early-boot VMM setup: adopt the assembly-built early_l1 table
 * into a real kernel AddressSpace, then later drop the identity mapping
 * and lock the kernel region down to kernel-only access. */

#include "vmm_internal.h"

#include "core/panic.h"
#include "kernel/layout.h"
#include "kernel/mm/alloc.h"

#include <arch/asid.h>
#include <arch/barrier.h>
#include <arch/mmu.h>
#include <string.h>

extern kernel_layout_t kernel_layout;
extern uint32_t early_l1[];

#define LOG_FMT(fmt) "(vmm) " fmt
#include <zuzu/log.h>

void VmmBootstrap(void) {
    if (!g_kernel_as) {
        g_kernel_as = KZAlloc(sizeof(AddressSpace));
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

        // Record identity mapping so VmmRemoveIdentityMapping can find it
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

    VmmUnmapRange(g_kernel_as, map_pa_start, map_size, true);
    KDEBUG("identity unmapped, pruning region");

    VirtMemRegion *r = VmmFindRegion(g_kernel_as, map_pa_start);
    if (r) {
        VmmRemoveRegion(g_kernel_as, r->vaddr_start, r->size);
    }

    KDEBUG("identity mapping removed, running pure higher-half");
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
            entry &= ~(0x3U << 10);
            entry |=  (0x1U << 10);
            l1[i] = entry;
            continue;
        }

        // Coarse page table (bits[1:0] == 0b01)
        if ((entry & 0x3) == 0x1) {
            PhysAddr l2_pa = entry & 0xFFFFFC00U;
            VirtAddr *l2 = (VirtAddr *)PA_TO_VA(l2_pa);

            for (size_t j = 0; j < 256; j++) {
                uint32_t pte = l2[j];

                // Small page descriptor has bits[1:0] == 0b10
                if ((pte & 0x3) != 0x2) {
                    continue;
                }

                // Small page AP bits are [5:4]. Force kernel-only AP=01.
                pte &= ~(0x3U << 4);
                pte |=  (0x1U << 4);
                l2[j] = pte;
            }
        }
    }

    ArchCtxSync();

    // Flush TLB so old permissions are gone
    arch_mmu_flush_tlb();

    ArchCtxSync();
}
