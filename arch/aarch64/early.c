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
#include <arch/irq.h>
#include <arch/cpu.h>
#include <arch/platform.h>
#include <arch/symbols.h>
#include <string.h>

#define LOG_TAG "(early)"
#include "core/log.h"

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
    ArchCpuHalt();
}
