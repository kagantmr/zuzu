/**
 * @file cpu.h
 * @brief Architecture-netural CPU state.
 */

#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <stdint.h>

/**
 * Include arch implementation. 
 */
#include <arch_impl/cpu.h>

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
uintptr_t ArchCurrentStackPointer(void);

#endif // ARCH_CPU_H
