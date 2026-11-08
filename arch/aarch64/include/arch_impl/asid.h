/**
 * @file
 * @brief AArch64 ASID width.
 */

#ifndef ARCH_AARCH64_IMPL_ASID_H
#define ARCH_AARCH64_IMPL_ASID_H

#include <stdint.h>

/** @brief An ASID; 16 bits, with TCR_EL1.AS set by the boot code. */
typedef uint16_t Asid;

#endif // ARCH_AARCH64_IMPL_ASID_H
