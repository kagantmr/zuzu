// Early boot code for Versatile Express A15
#include "arch/arm/include/symbols.h"
#include "arch/arm/include/irq.h"
#include "arch/arm/vexpress-a15/board.h"
#include "arch/arm/mmu/mmu.h"
#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "kernel/kmain.h"
#include "kernel/dtb/dtb.h"
#include "kernel/boot_info.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/vmm.h"
#include "core/panic.h"
#include <assert.h>
#include <mem.h>
#include <string.h>

kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
extern addrspace_t *g_kernel_as;
#define SECTION_NORMAL_DESC 0x11C0Eu

#define LOG_FMT(fmt) "(early) " fmt
#include "core/log.h"

__attribute__((section(".bss.boot"), aligned(16384))) uint32_t early_l1[4096];

static void early_map_ram_sections(uintptr_t ram_base, size_t ram_size) {
    uint32_t *l1 = (uint32_t *)PA_TO_VA((uintptr_t)early_l1);
    uintptr_t pa_start = ram_base & ~(SECTION_SIZE - 1);
    uintptr_t pa_end = (ram_base + ram_size + SECTION_SIZE - 1) & ~(SECTION_SIZE - 1);

    for (uintptr_t pa = pa_start; pa < pa_end; pa += SECTION_SIZE) {
        uint32_t entry = (uint32_t)pa | SECTION_NORMAL_DESC;
        l1[(pa >> 20) & 0xFFFu] = entry;
        l1[(PA_TO_VA(pa) >> 20) & 0xFFFu] = entry;
    }

    arch_mmu_flush_tlb();
    arch_mmu_barrier();
}

static void pmu_init() {
    uint32_t pmcr;
    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));
    pmcr |= 1;        // enable PMU
    pmcr |= (1 << 2); // reset cycle counter
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" ::"r"(pmcr));
    __asm__ volatile("mcr p15, 0, %0, c9, c14, 0" ::"r"(0x00000001));
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" ::"r"(0x80000000)); // enable cycle counter

}

static void vfp_init() {
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr |= (0xF << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" ::"r"(cpacr));
    __asm__ volatile("isb");
    __asm__ volatile(
        ".fpu vfpv4\n\t"
        "vmsr fpexc, %0" ::"r"(1u << 30));
}

int rdcyc() {
    uint32_t value;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(value));
    return value;
}

_Noreturn void early(void *dtb_ptr)
{
    dtb_init((const void *)PA_TO_VA((uintptr_t)dtb_ptr));

    kernel_layout.dtb_start_pa = (uintptr_t)dtb_ptr;
    kernel_layout.kernel_start_pa = (uintptr_t)_kernel_phys_start;
    kernel_layout.kernel_end_pa = (uintptr_t)_kernel_phys_end;
    kernel_layout.stack_base_pa = (uintptr_t)__svc_stack_base__;
    kernel_layout.stack_top_pa = (uintptr_t)__svc_stack_top__;

    uint64_t ram_base, ram_size;
    if (!dtb_get_reg("/memory", 0, &ram_base, &ram_size))
        panic("Failed to find memory cell in DTB");

    kernel_layout.ram_start = (uintptr_t)ram_base;
    kernel_layout.ram_end = (uintptr_t)(ram_base + ram_size);
    early_map_ram_sections(kernel_layout.ram_start, (size_t)ram_size);

    pmm_init();
    kheap_init();
    vmm_bootstrap();

    /* fill kernel layout VAs now that paging/higher-half mapping exists */
    kernel_layout.dtb_start_va = (void *)PA_TO_VA(kernel_layout.dtb_start_pa);
    kernel_layout.stack_base_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_base_pa);
    kernel_layout.stack_top_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_top_pa);
    kernel_layout.kernel_start_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_start_pa);
    kernel_layout.kernel_end_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_end_pa);

    boot_info_init_from_dtb();

    /* The boot-only sections are no longer needed once the PMM-backed
     * kernel L1 is live and DTB data has been copied out.
     */
    pmm_unmark_range(kernel_layout.dtb_start_pa, (paddr_t)_boot_end);

    vfp_init();
    pmu_init();

    vmm_remove_identity_mapping();
    arch_mmu_init_ttbr1(vmm_get_kernel_as());
    vmm_lockdown_kernel_sections();

    irq_init();
    board_init_devices();

    KINFO("Freed DTB and boot space (%zu KiB)", ((paddr_t)_boot_end - kernel_layout.dtb_start_pa) / 1024);
    KINFO("Boot info initialized from DTB: dev_count=%u", boot_info_dev_count());
    KINFO("Handoff to kmain");
    kmain();
}
