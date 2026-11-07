/**
 * @file
 * @brief ARMv7-A thread pointer read.
 */

#ifndef ARCH_ARM_IMPL_TLS_H
#define ARCH_ARM_IMPL_TLS_H

#include <stdint.h>
#include <arch_impl/sysreg.h>

/**
 * @brief Reads the user read-only thread ID register (TPIDRURO).
 *
 * @return The current task's info block address.
 */
static inline uintptr_t ArchGetTlsPointer(void)
{
    return READ_SYSREG(tpidrro_el0);
}

#endif // ARCH_ARM_IMPL_TLS_H
