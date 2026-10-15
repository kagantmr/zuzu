#include "core/kprintf.h"
#include "core/panic.h"
#include "kernel/boot_info.h"
#include "kernel/dev/fdt_wrappers.h"
#include "kernel/kmain.h"
#include "kernel/layout.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/pmm/pmm.h"
#include "kernel/mm/vmm/vmm.h"
#include <arch/barrier.h>
#include <arch/irq.h>
#include <arch/mmu.h>
#include <arch/platform.h>
#include <arch/symbols.h>
#include <arch_impl/armv7_mmu.h>
#include <string.h>

RamLayout kernel_layout;

#define LOG_FMT(fmt) "(early) " fmt
#include "core/log.h"

__attribute__((section(".bss.boot"), aligned(16384))) uint32_t early_l1[4096];

static void early_map_ram_sections(uintptr_t ram_base, size_t ram_size)
{
    uint32_t *l1 = (uint32_t *)PA_TO_VA((uintptr_t)early_l1);
    uintptr_t pa_start = ram_base & ~(SECTION_SIZE - 1);
    uintptr_t pa_end = (ram_base + ram_size + SECTION_SIZE - 1) & ~(SECTION_SIZE - 1);

    for (uintptr_t pa = pa_start; pa < pa_end; pa += SECTION_SIZE) {
        uint32_t entry = (uint32_t)pa | L1_SECT_BOOT_NORMAL;
        l1[L1_IDX(pa)] = entry;
        l1[L1_IDX(PA_TO_VA(pa))] = entry;
    }

    ArchMmuFlushTlb();
    ArchSyncBarrier();
}

static void pmu_init(void)
{
    uint32_t pmcr;
    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));
    pmcr |= (1 << 0);
    pmcr |= (1 << 2);
    pmcr &= ~(1u << 3);
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0" ::"r"(pmcr));
    __asm__ volatile("mcr p15, 0, %0, c9, c14, 0" ::"r"(0x00000001));
    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1" ::"r"(0x80000000));
}

static void vfp_init(void)
{
    uint32_t cpacr;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 2" : "=r"(cpacr));
    cpacr |= (0xF << 20);
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 2" ::"r"(cpacr));
    __asm__ volatile("isb");
    __asm__ volatile(".fpu vfpv4\n\t"
                     "vmsr fpexc, %0" ::"r"(1u << 30));
}

int rdcyc(void);
int rdcyc(void)
{
    uint32_t value;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(value));
    return (int)value;
}

_Noreturn void early(void *dtb_ptr);
_Noreturn void early(void *dtb_ptr)
{
    
    kprintf_init(ArchEarlyPutc);

    KDEBUG("early: dtb pa=%p", dtb_ptr);
    FdtInit((const void *)PA_TO_VA((PhysAddr)dtb_ptr));
    KDEBUG("early: dtb parsed (%u bytes)", FdtTotalSize());

    kernel_layout.dtb_start_pa = (PhysAddr)dtb_ptr;
    kernel_layout.kernel_start_pa = (PhysAddr)_kernel_phys_start;
    kernel_layout.kernel_end_pa = (PhysAddr)_kernel_phys_end;
    kernel_layout.stack_base_pa = (PhysAddr)__svc_stack_base__;
    kernel_layout.stack_top_pa = (PhysAddr)__svc_stack_top__;

    uint64_t ram_base, ram_size;
    if (!FdtGetReg("/memory", 0, &ram_base, &ram_size))
        panic("Failed to find memory cell in DTB");

    uint64_t linear_max = (uint64_t)(IOREMAP_BASE - PA_TO_VA((PhysAddr)ram_base));
    if (ram_size > linear_max)
        ram_size = linear_max;

    kernel_layout.ram_start = (PhysAddr)ram_base;
    kernel_layout.ram_end = (PhysAddr)(ram_base + ram_size);
    KDEBUG("early: ram [%p..%p)", (void *)kernel_layout.ram_start, (void *)kernel_layout.ram_end);
    early_map_ram_sections(kernel_layout.ram_start, (size_t)ram_size);

    KDEBUG("early: pmm");
    PmmInit();
    KDEBUG("early: kheap");
    KHeapInit();
    KDEBUG("early: vmm bootstrap");
    VmmBootstrap();

    kernel_layout.stack_base_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_base_pa);
    kernel_layout.stack_top_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_top_pa);
    kernel_layout.kernel_start_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_start_pa);
    kernel_layout.kernel_end_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_end_pa);

    PhysAddr dtb_end_pa = kernel_layout.dtb_start_pa + FdtTotalSize();
    struct {
        uint64_t addr, size;
    } rsv[8];
    uint32_t rsv_cnt = 0;
    while (rsv_cnt < 8 && FdtGetReservedMem(rsv_cnt, &rsv[rsv_cnt].addr, &rsv[rsv_cnt].size))
        rsv_cnt++;

    BootInfoInitFromFdt();

    PmmUnmarkRange(kernel_layout.dtb_start_pa, dtb_end_pa);
    PmmUnmarkRange((PhysAddr)_boot_start, (PhysAddr)_boot_end);

    for (uint32_t i = 0; i < rsv_cnt; i++)
        PmmMarkRange((PhysAddr)rsv[i].addr, (PhysAddr)(rsv[i].addr + rsv[i].size));

    KDEBUG("early: boot_info done (%u devs), vfp/pmu", boot_info_dev_count());
    vfp_init();
    pmu_init();

    KDEBUG("early: dropping identity map");
    VmmRemoveIdentityMapping();
    KDEBUG("early: ttbr1 split");
    ArchMmuInitTtbr1(VmmGetKernelAddressSpace());
    KDEBUG("early: kernel lockdown");
    VmmLockdownKernelMapping();

    KDEBUG("early: irq + platform devices");
    ArchIrqInit();
    ArchPlatformInitDevices();

    KINFO("Freed DTB and boot space (%zu KiB)",
          ((PhysAddr)_boot_end - (PhysAddr)_boot_start + dtb_end_pa - kernel_layout.dtb_start_pa) /
              1024);
    KDEBUG("Boot info initialized from DTB: dev_count=%u", boot_info_dev_count());
    KDEBUG("Handoff to kmain");
    kmain();
}
