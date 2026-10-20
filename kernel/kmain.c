/**
 * @file kmain.c
 * @brief Kernel main: initialises the scheduler, syspage and boot info, then loads the root service.
 */

#include "kernel/kmain.h"

#include <arch/cpu.h>
#include <arch/symbols.h>

#include <arch/mmu.h>
#include <arch/platform.h>

#include <arch/timer.h>

#include "boot_info.h"
#include "core/panic.h"
#include "kernel/sched/sched.h"

#include "core/version.h"
#include "kernel/loader/boot_programs.h"
#include "kernel/loader/initrd.h"
#include "kernel/syspage.h"
#include "types.h"
#include <stdint.h>
#include <util/zxf.h>

#define LOG_FMT(fmt) "(main) " fmt
#include "core/log.h"

_Noreturn void kmain(void)
{
    KINFO("Booting %s", "zuzu-" ZUZU_CODENAME "-" ZUZU_VERSION);

    SchedInit();
    ArchGlobalIrqEnable();

    SyspageInit();
    BootInfoInit();

    uint64_t chosen_pa, chosen_size;
    if (!boot_info_initrd(&chosen_pa, &chosen_size))
        panic("No bootloader-supplied initrd (DTB /chosen)");

    PhysAddr initrd_pa = (PhysAddr)chosen_pa;
    size_t initrd_size = (size_t)chosen_size;
    KINFO("initrd: bootloader-supplied at pa=%p size=%zu", (void *)(uintptr_t)initrd_pa,
          initrd_size);

    InitrdInit((const void *)PA_TO_VA((uintptr_t)initrd_pa), initrd_size);

    CreateRootService(initrd_pa, initrd_size);

    KINFO("Kernel up");

    Schedule();

    panic("Unreachable: %s:%d", __FILE__, __LINE__);
}
