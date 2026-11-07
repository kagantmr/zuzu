/**
 * @file
 * @brief AArch64 early boot: from _start's first C call to kmain().
 */

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
#include <arch/cpu.h>
#include <arch/irq.h>
#include <arch/platform.h>
#include <arch/symbols.h>
#include <string.h>

#define LOG_TAG "(early)"
#include "core/log.h"

RamLayout kernel_layout;

/**
 * @brief Fills the RAM banks and the RAM span of kernel_layout from the device tree's @c /memory node.
 *
 * Banks are kept sorted by address and cut off where the linear map ends. Empty banks and banks
 * past RAM_BANKS_MAX are dropped.
 *
 * @return false if no usable bank was found.
 */
static bool ReadRamBanks(void)
{
    const uint64_t linear_end = (uint64_t)(IOREMAP_BASE - PA_TO_VA((PhysAddr)0));
    RamBank *banks = kernel_layout.ram_banks;
    size_t count = 0;

    uint64_t base, size;
    for (int i = 0; FdtGetReg("/memory", i, &base, &size); i++) {
        uint64_t end = base + size;
        if (end > linear_end)
            end = linear_end;
        base = align_up(base, PAGE_SIZE);
        end = align_down(end, PAGE_SIZE);
        if (base >= end)
            continue;
        if (count == RAM_BANKS_MAX) {
            KWARN("early: ignoring ram [%p..%p), too many banks", (void *)base, (void *)end);
            continue;
        }

        size_t at = count++;
        while (at > 0 && banks[at - 1].start > base) {
            banks[at] = banks[at - 1];
            at--;
        }
        banks[at] = (RamBank){.start = (uintptr_t)base, .end = (uintptr_t)end};
    }

    kernel_layout.ram_bank_count = count;
    if (count == 0)
        return false;

    kernel_layout.ram_start = banks[0].start;
    kernel_layout.ram_end = banks[count - 1].end;
    for (size_t i = 0; i < count; i++)
        KDEBUG("early: ram bank %u [%p..%p)", (unsigned)i, (void *)banks[i].start,
               (void *)banks[i].end);
    return true;
}

static void EarlyTick(void)
{
    static unsigned n = 0;
    KDEBUG("tick %u", ++n);
    ArchTimerSetDeadline(ArchTimerNow() + ArchTimerFreq() / 10);
}

/**
 * @brief First C function, called by _start with the MMU on.
 *
 * Parses the device tree, maps RAM, starts the PMM, heap and VMM, frees the boot
 * memory, drops the identity map, splits TTBR0/TTBR1, probes the board's devices
 * and calls kmain().
 *
 * @param[in] dtb_ptr Physical address of the device tree blob.
 */
_Noreturn void early(void *dtb_ptr);
_Noreturn void early(void *dtb_ptr)
{

    kprintf_init(ArchEarlyPutc);

    KDEBUG("early: dtb pa=%p", dtb_ptr);
    FdtInit((const void *)PA_TO_VA((PhysAddr)dtb_ptr));
    KDEBUG("early: dtb parsed (%u bytes)", FdtTotalSize());

    if (!ReadRamBanks()) {
        KFATAL("DTB has no usable memory");
        ArchCpuHalt();
    }

    kernel_layout.dtb_start_pa = (PhysAddr)dtb_ptr;
    kernel_layout.kernel_start_pa = VA_TO_PA((VirtAddr)_kernel_start);
    kernel_layout.kernel_end_pa   = VA_TO_PA((VirtAddr)_kernel_end);
    kernel_layout.stack_base_pa   = VA_TO_PA((VirtAddr)__kstack_base);
    kernel_layout.stack_top_pa    = VA_TO_PA((VirtAddr)__kstack_top);
    kernel_layout.boot_start_pa   = VA_TO_PA((VirtAddr)_boot_start_va);
    kernel_layout.boot_end_pa     = VA_TO_PA((VirtAddr)_boot_end_va);

    PmmInit();
    KHeapInit();
    VmmBootstrap();
    PmmAddBanks();

    kernel_layout.stack_base_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_base_pa);
    kernel_layout.stack_top_va = (uintptr_t)PA_TO_VA(kernel_layout.stack_top_pa);
    kernel_layout.kernel_start_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_start_pa);
    kernel_layout.kernel_end_va = (uintptr_t)PA_TO_VA(kernel_layout.kernel_end_pa);

    VmmRemoveIdentityMapping();
    VmmLockdownKernelMapping();
    PmmUnmarkRange(kernel_layout.boot_start_pa, kernel_layout.boot_end_pa);
    KDEBUG("early: running on the kernel's own tables");

    BootInfoInitFromFdt();
    ArchIrqInit();
    ArchPlatformInitDevices();

    ArchTimerSetHandler(EarlyTick);
    ArchTimerSetDeadline(ArchTimerNow() + (ArchTimerFreq() / 10));
    __asm__ volatile("msr daifclr, #2");
    for (;;)
        ArchCpuIdle();
}
