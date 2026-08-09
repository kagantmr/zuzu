// platform.c - vexpress-a15 device bring-up (arch_platform_init_devices).
//
// This is the board's implementation of the platform HAL hook. The *mechanism*
// is generic (find_dev walks the DTB device array and matches by compatible
// string), but the *device set* is board/SoC-specific: this board has a PL011
// UART, a GICv2-family interrupt controller, an optional PL031 RTC, and uses the
// ARM generic timer as its tick source. A board with different peripherals
// (e.g. a non-PL011 UART or GICv3) provides its own version of this file.
//
// The compatible-string tables absorb naming differences across closely related
// boards that share these peripherals (e.g. GICv2 named "arm,cortex-a15-gic" on
// vexpress vs "arm,gic-400" on the Pi 4), but they do not make this file
// board-independent.

#include "kernel/dev/fdt_wrappers.h"
#include "kernel/mm/vmm.h"
#include "kernel/boot_info.h"
#include "drivers/uart/pl011.h"
#include "drivers/uart/uart.h"
#include <arch/platform.h>
#include "arch/arm/include/gicv2.h"
#include <arch/timer.h>
#include "core/panic.h"
#include "core/kprintf.h"
#include <string.h>

#define LOG_FMT(fmt) "(board) " fmt
#include "core/log.h"

uint32_t rtc_epoch;

// Early console: poke the PL011 through the bootstrap peripheral section at
// its identity VA (see early_paging_init's PERIPH_PA_BASE/PERIPH_MB in
// _start.S). Those L1 entries survive the RAM identity unmap (which only
// clears RAM sections) and are copied into the kernel L1 by
// vmm_bootstrap, so this works from the top of early() until the ioremapped
// driver takes over below. QEMU's vexpress-a15 model values, not real
// hardware — this board has none (see README's Supported Targets).
#define EARLY_UART      ((volatile uint32_t *)0x1C090000u)
#define EARLY_UART_FR   (0x18u / 4u)
#define EARLY_UART_TXFF (1u << 5)

void arch_early_putc(char c) {
    if (c == '\n')
        arch_early_putc('\r');
    while (EARLY_UART[EARLY_UART_FR] & EARLY_UART_TXFF)
        ;
    EARLY_UART[0] = (uint32_t)(uint8_t)c;
}

// Find the first DTB device whose compatible string matches any entry in the
// NULL-terminated list. Returns the device, or NULL if none matched.
static const FdtDevice *find_dev(const char *const *compat) {
    const FdtDevice *arr = boot_info_dev_array();
    uint32_t cnt = boot_info_dev_count();
    for (uint32_t i = 0; i < cnt; i++) {
        for (const char *const *c = compat; *c; c++) {
            if (strcmp(arr[i].compatible, *c) == 0)
                return &arr[i];
        }
    }
    return NULL;
}

// Compatible strings, ordered most-to-least preferred where it matters.
static const char *const PL011_COMPAT[] = { "arm,pl011", NULL };
static const char *const GIC_COMPAT[]   = { "arm,gic-400", "arm,cortex-a15-gic",
                                            "arm,gic-v2", NULL };
static const char *const PL031_COMPAT[] = { "arm,pl031", NULL };

void arch_platform_init_devices(void) {
    const FdtDevice *d;

    // UART (PL011): present on vexpress and the Pi 4 alike.
    if ((d = find_dev(PL011_COMPAT))) {
        void *uart_va = IoRemap((uintptr_t)d->phys, (size_t)d->size);
        if (!uart_va) panic("Failed to ioremap UART");

        uart_set_driver(&pl011_driver, (uintptr_t)uart_va);
        kprintf_init(uart_putc);

        KDEBUG("UART re-mapped to %p", uart_va);
    }

    // Interrupt controller (GICv2 family).
    if (!(d = find_dev(GIC_COMPAT))) panic("GIC not found");
    {
        uint64_t gicd = d->phys, s_d = d->size;
        uint64_t gicc = 0, s_c = 0;
        if (d->nregs >= 2) {
            gicc = d->phys2;
            s_c = d->size2;
        }

        void *gicd_va = IoRemap(gicd, s_d);
        void *gicc_va = IoRemap(gicc ? gicc : gicd, s_c ? s_c : s_d);

        KDEBUG("GICv2 (GICD) re-mapped to %p", gicd_va);

        if (!gicd_va || !gicc_va) panic("Failed to ioremap GIC");

        gic_init((uintptr_t)gicd_va, (uintptr_t)gicc_va);
    }

    // RTC (PL031): optional. The Pi 4 has no RTC, so its absence is fine.
    if ((d = find_dev(PL031_COMPAT))) {
        void *rtc_va = IoRemap((uintptr_t)d->phys, (size_t)d->size);
        if (rtc_va) {
            rtc_epoch = *((volatile uint32_t *)rtc_va);
            if (rtc_epoch == 0)
                KDEBUG("Oops! RTC epoch is 0 (check for anomalies)");
            IoUnmap(rtc_va);
        }
    } else {
        KDEBUG("No RTC found in DTB, epoch is 0");
    }

    KDEBUG("Using ARM generic timer as tick source");
    arch_timer_init();
}
