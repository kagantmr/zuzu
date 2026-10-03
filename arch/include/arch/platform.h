// arch/platform.h - Neutral platform/device bring-up contract.
//
// Discover and initialize the platform's core devices (console UART, interrupt
// controller, timer, RTC, ...). On ARM this is DTB-driven and board-independent.

#ifndef ZUZU_ARCH_PLATFORM_H
#define ZUZU_ARCH_PLATFORM_H

/** Discover and initialize platform devices. Called once during early boot. */
void arch_platform_init_devices(void);

/** Best-effort console character output usable from the top of early boot,
 * before device discovery. arch/arm/platform.c writes the board's UART0. */
void arch_early_putc(char c);

#endif // ZUZU_ARCH_PLATFORM_H
