/**
 * generic_timer.h - ARM Generic Timer private definitions.
 *
 * The neutral entry point (arch_timer_init) is declared in <arch/timer.h>; this
 * header holds ARM-private constants for the Generic Timer implementation.
 *
 * The driver uses the ARMv7-A Generic Timer, which provides two timer sources:
 * - CNTP (Physical Non-secure) timer, the primary tick source.
 * - CNTV (Virtual) timer, also delivered by some QEMU setups; when both are
 *   present zuzu runs them in a dual-source configuration (see
 *   generic_timer_handler in generic_timer.c).
 * The driver fires interrupts at a 10ms interval.
 */

#ifndef ARCH_ARM_GENERIC_TIMER_H
#define ARCH_ARM_GENERIC_TIMER_H

#include <stdint.h>

#define TIMER_IRQ_VIRT  27   /* CNTV PPI */

#endif // ARCH_ARM_GENERIC_TIMER_H
