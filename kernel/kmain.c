#include "kernel/kmain.h"

#include <arch/symbols.h>
#include <arch/cpu.h>

#include <arch/mmu.h>
#include <arch/platform.h>

#include "core/panic.h"
#include "kernel/layout.h"
#include "boot_info.h"
#include "kernel/time/tick.h"
#include "kernel/sched/sched.h"

#include "kernel/loader/initrd.h"
#include "kernel/loader/boot_programs.h"
#include "core/version.h"
#include "kernel/syspage.h"
#include "zuzu/types.h"
#include <stdint.h>
#include <zuzu/zxf.h>

#define STR(x) #x
#define XSTR(x) STR(x)


#define LOG_FMT(fmt) "(main) " fmt
#include "core/log.h"

extern kernel_layout_t kernel_layout;

/* register_tick_callback keeps a single slot (see kernel/time/tick.c), so
 * this wraps set_resched_flag rather than being registered alongside it —
 * a second call to register_tick_callback would silently replace the first
 * and stop preemption. */
static void sched_tick(void)
{
    set_resched_flag();
}

_Noreturn void kmain(void)
{
    KINFO("Booting %s", "zuzu-" ZUZU_CODENAME "-" ZUZU_VERSION);
    /* DTB and boot_info were initialized in early(); do not touch DTB again */

    sched_init();
    arch_global_irq_enable();

    SyspageInit();

    /* The initrd always comes from the bootloader/firmware now (u-boot's
     * bootm, or the Pi firmware on rpi4), via the DTB /chosen node. */
    uint64_t chosen_pa, chosen_size;
    if (!boot_info_initrd(&chosen_pa, &chosen_size))
        panic("No bootloader-supplied initrd (DTB /chosen)");

    PhysAddr initrd_pa = (PhysAddr)chosen_pa;
    size_t initrd_size = (size_t)chosen_size;
    KINFO("initrd: bootloader-supplied at pa=%p size=%zu",
          (void *)(uintptr_t)initrd_pa, initrd_size);
    SyspageSetInitrdSz((uint32_t)initrd_size);

    initrd_init((const void *)PA_TO_VA((uintptr_t)initrd_pa), initrd_size);

    /* Load and spawn every boot program listed in boot.manifest (see
     * kernel/loader/boot_programs.c). */
    boot_programs_spawn_all(initrd_pa, initrd_size);

    register_tick_callback(sched_tick);

    KINFO("Entering idle");


    schedule();
    
    panic("Unreachable: %s:%d", __FILE__, __LINE__);
}
