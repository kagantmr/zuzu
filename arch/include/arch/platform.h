// arch/platform.h - Neutral platform/device bring-up contract.
//
// Discover and initialize the platform's core devices (console UART, interrupt
// controller, timer, RTC, ...). On ARM this is DTB-driven and board-independent.

#ifndef ARCH_PLATFORM_H
#define ARCH_PLATFORM_H

/** Discover and initialize platform devices. Called once during early boot. */
void ArchPlatformInitDevices(void);

/** Best-effort console character output usable from the top of early boot,
 * before device discovery. arch/arm/platform.c writes the board's UART0. */
void arch_early_putc(char c);

#endif // ARCH_PLATFORM_H
