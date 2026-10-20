/**
 * @file
 * @brief ARMv7-A thread pointer read.
 */

#ifndef ARCH_ARM_IMPL_TLS_H
#define ARCH_ARM_IMPL_TLS_H

#include <stdint.h>

/**
 * @brief Reads the user read-only thread ID register (TPIDRURO).
 *
 * @return The current task's info block address.
 */
static inline uintptr_t ArchGetTlsPointer(void)
{
    uintptr_t tp;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tp));
    return tp;
}

#endif // ARCH_ARM_IMPL_TLS_H
