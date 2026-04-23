/**
 * generic_timer.h - ARM Generic Timer interface
 * This driver uses the ARMv7-A Generic Timer, which provides two timer sources:
 * - CNTP (Physical Non-secure) timer, which counts at a fixed frequency and
 * generates an interrupt to the physical CPU. This is the primary timer source.
 * - CNTV (Virtual) timer, which also counts at the same frequency but generates
 * interrupts to the virtual CPU. Some QEMU setups deliver this as well, and if
 * both are present, zuzuOS will use them in a dual-source configuration for better timer
 * interrupt handling (see generic_timer_handler in generic_timer.c).
 * The driver initializes the timers to generate interrupts at a 10ms interval and
 * provides a function to read the current tick count.
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/**
 * @brief Initialize the ARM Generic Timer to generate periodic interrupts.
 * Sets up both the physical and virtual timers (if available) to fire every 10ms
 * and registers the interrupt handler. The handler will announce ticks to the kernel's tick subsystem.
 * Note: If both timers are present and enabled, the effective tick rate may double, which can improve timer interrupt handling but may also cause unexpected behavior.
 *
 */
void generic_timer_init(void);
/**
 * @brief Get the current tick count from the generic timer.
 * @return Current tick count as a 64-bit value.
 */
uint64_t generic_timer_get_ticks(void);

#endif // TIMER_H