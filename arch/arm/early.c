// Early boot init (board-independent ARM path)
//
// Runs in the higher half right after _start.S enables the MMU. The sequence
// here is the same for every ARM board: parse the DTB, map RAM, bring up the
// PMM/heap/VMM, tear down the identity mapping, then init IRQs and devices.
// Anything board-specific is supplied by the DTB and the per-board layout.h /
// linker.ld / _start.S, so this file does not change when adding a board.
#include <arch/symbols.h>
#include <arch/irq.h>
#include <arch/platform.h>
#include <arch/mmu.h>
#include "kernel/layout.h"
#include "kernel/mm/pmm.h"
#include "kernel/kmain.h"
#include "kernel/dtb/dtb.h"
#include "kernel/boot_info.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/vmm.h"
#include "core/panic.h"
#include "core/kprintf.h"
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

/* Default early console: drop the character. Boards with a fixed debug UART
 * (e.g. rpi4) provide a strong override so panics and progress are visible
 * before arch_platform_init_devices() wires up the real console. */
__attribute__((weak)) void arch_early_putc(char c) { (void)c; }

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
    /* Console-before-everything: a no-op unless the board overrides it.
     * arch_platform_init_devices() replaces the sink with the real driver. */
    kprintf_init(arch_early_putc);

    KINFO("early: dtb pa=%p", dtb_ptr);
    dtb_init((const void *)PA_TO_VA((uintptr_t)dtb_ptr));
    KINFO("early: dtb parsed (%u bytes)", dtb_total_size());

    kernel_layout.dtb_start_pa = (uintptr_t)dtb_ptr;
    kernel_layout.kernel_start_pa = (uintptr_t)_kernel_phys_start;
    kernel_layout.kernel_end_pa = (uintptr_t)_kernel_phys_end;
    kernel_layout.stack_base_pa = (uintptr_t)__svc_stack_base__;
    kernel_layout.stack_top_pa = (uintptr_t)__svc_stack_top__;

    uint64_t ram_base, ram_size;
    if (!dtb_get_reg("/memory", 0, &ram_base, &ram_size))
        panic("Failed to find memory cell in DTB");

    /* The kernel linear map covers [PA_TO_VA(ram_base), IOREMAP_BASE); RAM
     * beyond that window (e.g. the Pi 4's 2-8 GiB) is unusable without
     * highmem support, and mapping it would wrap into the ioremap region. */
    uint64_t linear_max = (uint64_t)(IOREMAP_BASE - PA_TO_VA((uintptr_t)ram_base));
    if (ram_size > linear_max)
        ram_size = linear_max;

    kernel_layout.ram_start = (uintptr_t)ram_base;
    kernel_layout.ram_end = (uintptr_t)(ram_base + ram_size);
    KINFO("early: ram [%p..%p)", (void *)kernel_layout.ram_start,
          (void *)kernel_layout.ram_end);
    early_map_ram_sections(kernel_layout.ram_start, (size_t)ram_size);

    KINFO("early: pmm");
    pmm_init();
    KINFO("early: kheap");
    kheap_init();
    KINFO("early: vmm bootstrap");
    vmm_bootstrap();

    /* fill kernel layout VAs now that paging/higher-half mapping exists */
    kernel_layout.dtb_start_va = (void *)PA_TO_VA(kernel_layout.dtb_start_pa);
    kernel_layout.stack_base_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_base_pa);
    kernel_layout.stack_top_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_top_pa);
    kernel_layout.kernel_start_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_start_pa);
    kernel_layout.kernel_end_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_end_pa);

    /* boot_info_init_from_dtb() copies everything out of the DTB and shuts
     * down libfdt access, so capture what the cleanup below needs first. */
    paddr_t dtb_end_pa = kernel_layout.dtb_start_pa + dtb_total_size();
    struct { uint64_t addr, size; } rsv[8];
    uint32_t rsv_cnt = 0;
    while (rsv_cnt < 8 && dtb_get_memrsv(rsv_cnt, &rsv[rsv_cnt].addr, &rsv[rsv_cnt].size))
        rsv_cnt++;

    boot_info_init_from_dtb();

    /* The boot-only sections and the DTB are no longer needed once the
     * PMM-backed kernel L1 is live and DTB data has been copied out. The DTB
     * is freed by its own extent: it need not be adjacent to the kernel.
     */
    pmm_unmark_range(kernel_layout.dtb_start_pa, dtb_end_pa);
    pmm_unmark_range((paddr_t)_boot_start, (paddr_t)_boot_end);

    /* Re-assert firmware-reserved ranges in case they overlap what was just
     * freed (e.g. spin tables sharing a page with a low-memory DTB). */
    for (uint32_t i = 0; i < rsv_cnt; i++)
        pmm_mark_range((paddr_t)rsv[i].addr, (paddr_t)(rsv[i].addr + rsv[i].size));

    KINFO("early: boot_info done (%u devs), vfp/pmu", boot_info_dev_count());
    vfp_init();
    pmu_init();

    KINFO("early: dropping identity map");
    vmm_remove_identity_mapping();
    KINFO("early: ttbr1 split");
    arch_mmu_init_ttbr1(vmm_get_kernel_as());
    KINFO("early: kernel lockdown");
    vmm_lockdown_kernel_sections();

    KINFO("early: irq + platform devices");
    arch_irq_init();
    arch_platform_init_devices();

    KINFO("Freed DTB and boot space (%zu KiB)",
          ((paddr_t)_boot_end - (paddr_t)_boot_start + dtb_end_pa - kernel_layout.dtb_start_pa) / 1024);
    KINFO("Boot info initialized from DTB: dev_count=%u", boot_info_dev_count());
    KINFO("Handoff to kmain");
    kmain();
}
