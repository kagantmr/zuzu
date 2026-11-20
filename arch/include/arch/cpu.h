/**
 * @file cpu.h
 * @brief Architecture-netural CPU state.
 */

#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <stdbool.h>
#include <stdint.h>
#include <types.h>

/**
 * Include arch implementation. 
 */
#include <arch_impl/cpu.h>

/**
 * @brief Check whether an IRQ is waiting to be taken. Works with IRQs masked.
 *
 * @return true if an IRQ is pending at this CPU.
 */
bool ArchIrqPending(void);

/**
 * @brief Wait until an interrupt occurs.
 */
void ArchCpuIdle(void);

/**
 * @brief Disable interrupts, and spin in an interrupt waiting loop.
 */
_Noreturn void ArchCpuHalt(void);

/**
 * @brief Get the current stack pointer value.
 * 
 * @return The stack pointer value.
 */
VirtAddr ArchCurrentStackPointer(void);

#endif // ARCH_CPU_H
