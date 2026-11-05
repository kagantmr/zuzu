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

    uint64_t base, size;
    if (!FdtGetReg("/memory", 0, &base, &size)) {
        KFATAL("DTB corrupt");
        ArchCpuHalt();
    }

    kernel_layout.dtb_start_pa = (PhysAddr)dtb_ptr;
    kernel_layout.kernel_start_pa = VA_TO_PA((VirtAddr)_kernel_start);
    kernel_layout.kernel_end_pa   = VA_TO_PA((VirtAddr)_kernel_end);
    kernel_layout.stack_base_pa   = VA_TO_PA((VirtAddr)__kstack_base);
    kernel_layout.stack_top_pa    = VA_TO_PA((VirtAddr)__kstack_top);
    kernel_layout.boot_start_pa   = VA_TO_PA((VirtAddr)_boot_start_va);
    kernel_layout.boot_end_pa     = VA_TO_PA((VirtAddr)_boot_end_va);

    uint64_t linear_max = (uint64_t)(IOREMAP_BASE - PA_TO_VA((PhysAddr)base));
    if (size > linear_max)
        size = linear_max;

    kernel_layout.ram_start = (PhysAddr)base;
    kernel_layout.ram_end = (PhysAddr)(base + size);
    KDEBUG("early: ram [%p..%p)", (void *)kernel_layout.ram_start, (void *)kernel_layout.ram_end);
    // EarlyMapRamSections(kernel_layout.ram_start, (size_t)size);
    PmmInit();
    KHeapInit();

    ArchCpuHalt();
}
