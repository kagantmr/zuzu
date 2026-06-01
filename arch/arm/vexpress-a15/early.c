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
#include "drivers/uart/uart.h"
#include "drivers/uart/pl011.h"

kernel_layout_t kernel_layout;
extern pmm_state_t pmm_state;
extern addrspace_t *g_kernel_as;
#define LOG_FMT(fmt) "(early) " fmt
#include "core/log.h"
__attribute__((section(".bss.boot"), aligned(16384))) uint32_t early_l1[4096];

#define L1_TYPE_SECTION 0x2
#define L1_AP_FULL_ACCESS ((0 << 15) | (3 << 10))
#define L1_MEM_NORMAL ((1 << 12) | (1 << 3) | (1 << 2))
#define L1_MEM_DEVICE ((0 << 12) | (0 << 3) | (1 << 2))
#define L1_SHAREABLE (1 << 16)
#define L1_XN (1 << 4)
#define L1_DOMAIN_0 (0 << 5)
#define SECTION_NORMAL(pa)                                       \
    (((pa) & 0xFFF00000) | L1_TYPE_SECTION | L1_AP_FULL_ACCESS | \
     L1_MEM_NORMAL | L1_SHAREABLE | L1_DOMAIN_0)
#define SECTION_DEVICE(pa)                                       \
    (((pa) & 0xFFF00000) | L1_TYPE_SECTION | L1_AP_FULL_ACCESS | \
     L1_MEM_DEVICE | L1_SHAREABLE | L1_XN | L1_DOMAIN_0)
#define RAM_PA_BASE 0x80000000
#define RAM_VA_BASE 0xC0000000
#define RAM_SIZE_MB 256
#define MMIO_BASE 0x1C000000
#define MMIO_END 0x20000000

__attribute__((section(".text.boot")))
uintptr_t early_paging_init(uintptr_t dtb_phys)
{
    (void)dtb_phys;
    for (int i = 0; i < 4096; i++)
        early_l1[i] = 0;
    for (unsigned int i = 0; i < RAM_SIZE_MB; i++)
    {
        uintptr_t pa = RAM_PA_BASE + (i << 20);
        unsigned int idx = (RAM_PA_BASE >> 20) + i;
        early_l1[idx] = SECTION_NORMAL(pa);
    }
    for (unsigned int i = 0; i < RAM_SIZE_MB; i++)
    {
        uintptr_t pa = RAM_PA_BASE + (i << 20);
        unsigned int idx = (RAM_VA_BASE >> 20) + i;
        early_l1[idx] = SECTION_NORMAL(pa);
    }
    unsigned int mmio_sections = (MMIO_END - MMIO_BASE) >> 20;
    for (unsigned int i = 0; i < mmio_sections; i++)
    {
        uintptr_t pa = MMIO_BASE + (i << 20);
        unsigned int idx = (MMIO_BASE >> 20) + i;
        early_l1[idx] = SECTION_DEVICE(pa);
    }
    return (uintptr_t)early_l1;
}

_Noreturn void early(void *dtb_ptr)
{
    dtb_init((const void *)PA_TO_VA((uintptr_t)dtb_ptr));
#if defined(EARLY_UART)
    uart_set_driver(&pl011_driver, VEXPRESS_SMB_BASE + VEXPRESS_UART0_OFF);
    kprintf_init(uart_putc);
    KINFO("Early UART initialized");
#endif
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
    pmm_init();
    kheap_init();
    vmm_bootstrap();

    /* fill kernel layout VAs now that paging/higher-half mapping exists */
    kernel_layout.dtb_start_va = (void *)PA_TO_VA(kernel_layout.dtb_start_pa);
    kernel_layout.stack_base_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_base_pa);
    kernel_layout.stack_top_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_top_pa);
    kernel_layout.kernel_start_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_start_pa);
    kernel_layout.kernel_end_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_end_pa);

    boot_info_init_from_dtb(dtb_ptr);
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr |= (0xF << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" ::"r"(cpacr));
    __asm__ volatile("isb");
    __asm__ volatile(
        ".fpu vfpv4\n\t"
        "vmsr fpexc, %0" ::"r"(1u << 30));

    // set up the PMU
    uint32_t pmcr;
    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));
    pmcr |= 1;        // enable PMU
    pmcr |= (1 << 2); // reset cycle counter
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" ::"r"(pmcr));
    __asm__ volatile("mcr p15, 0, %0, c9, c14, 0" ::"r"(0x00000001));
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" ::"r"(0x80000000)); // enable cycle counter

    vmm_remove_identity_mapping();
    arch_mmu_init_ttbr1(vmm_get_kernel_as());
    vmm_lockdown_kernel_sections();

    kmain();
}
