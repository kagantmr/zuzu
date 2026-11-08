/**
 * @file
 * @brief AArch64 counter read.
 */

#ifndef ARCH_AARCH64_IMPL_COUNTER_H
#define ARCH_AARCH64_IMPL_COUNTER_H

#include <stdint.h>
#include "barrier.h"
#include "sysreg.h"

/**
 * @brief Reads the virtual count (CNTVCT_EL0), after an isb so the read is not hoisted.
 *
 * @return The current counter value.
 */
static inline __attribute__((always_inline)) uint64_t ArchCounterRead(void)
{
	Isb(); 
	return READ_SYSREG(cntvct_el0);
}

#endif // ARCH_AARCH64_IMPL_COUNTER_H
