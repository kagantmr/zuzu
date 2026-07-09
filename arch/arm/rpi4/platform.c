// platform.c - Raspberry Pi 4 (BCM2711) device bring-up (arch_platform_init_devices).
//
// This is the board's implementation of the platform HAL hook. The *mechanism*
// is generic (find_dev walks the DTB device array and matches by compatible
// string), but the *device set* is board/SoC-specific: the Pi 4 has PL011
// UARTs (uart0 is the console, routed to GPIO14/15 by _start.S), a GIC-400
// (GICv2) whose "arm,gic-400" compatible the table below matches, and the ARM
// generic timer as tick source. There is no RTC on this board.
//
// The early console divisors (115200 @ 48 MHz) were programmed by _start.S;
// the PL011 driver preserves them.

#include "kernel/dtb/dtb.h"
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
// its identity VA. Those L1 entries survive the RAM identity unmap (which
// only clears RAM sections) and are copied into the kernel L1 by
// vmm_bootstrap, so this works from the top of early() until the ioremapped
// driver takes over below. Deliberately self-contained: .text.boot helpers
// like early_uart_putc are only reachable through the RAM identity map,
// which early() tears down mid-flight.
#define EARLY_UART      ((volatile uint32_t *)0xFE201000u)
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
static const dtb_dev_t *find_dev(const char *const *compat) {
    const dtb_dev_t *arr = boot_info_dev_array();
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
static const char *const PL011_COMPAT[] = { "arm,pl011", "arm,pl011-axi", NULL };
static const char *const GIC_COMPAT[]   = { "arm,gic-400", "arm,gic-v2", NULL };

void arch_platform_init_devices(void) {
    const dtb_dev_t *d;

    // Console UART (PL011). The DTB lists all five; uart0 comes first.
    if ((d = find_dev(PL011_COMPAT))) {
        void *uart_va = ioremap((uintptr_t)d->phys, (size_t)d->size);
        if (!uart_va) panic("Failed to ioremap UART");

        uart_set_driver(&pl011_driver, (uintptr_t)uart_va);
        kprintf_init(uart_putc);

        KDEBUG("UART re-mapped to %p", uart_va);
    }

    // Interrupt controller (GIC-400, GICv2-compatible).
    if (!(d = find_dev(GIC_COMPAT))) panic("GIC not found");
    {
        uint64_t gicd = d->phys, s_d = d->size;
        uint64_t gicc = 0, s_c = 0;
        if (d->nregs >= 2) {
            gicc = d->phys2;
            s_c = d->size2;
        }

        void *gicd_va = ioremap(gicd, s_d);
        void *gicc_va = ioremap(gicc ? gicc : gicd, s_c ? s_c : s_d);

        KDEBUG("GICv2 (GICD) re-mapped to %p", gicd_va);

        if (!gicd_va || !gicc_va) panic("Failed to ioremap GIC");

        gic_init((uintptr_t)gicd_va, (uintptr_t)gicc_va);
    }

    // No RTC on the Pi 4; epoch stays 0 until set some other way.
    KDEBUG("No RTC on this board, epoch is 0");

    KDEBUG("Using ARM generic timer as tick source");
    arch_timer_init();
}
