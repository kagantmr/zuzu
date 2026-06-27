// arch/platform.h - Neutral platform/device bring-up contract.
//
// Discover and initialize the platform's core devices (console UART, interrupt
// controller, timer, RTC, ...). On ARM this is DTB-driven and board-independent.

#ifndef ZUZU_ARCH_PLATFORM_H
#define ZUZU_ARCH_PLATFORM_H

/** Discover and initialize platform devices. Called once during early boot. */
void arch_platform_init_devices(void);

#endif // ZUZU_ARCH_PLATFORM_H
