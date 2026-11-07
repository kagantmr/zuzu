/**
 * @file
 * @brief AArch64 system register access.
 */

#ifndef ARCH_AARCH64_IMPL_SYSREG_H
#define ARCH_AARCH64_IMPL_SYSREG_H

#include <stdint.h>

/**
 * @brief Reads a system register with mrs.
 *
 * @param name Register name as the assembler spells it, e.g. esr_el1.
 * @return The 64-bit register value.
 */
#define READ_SYSREG(name)                                                                          \
    ({                                                                                             \
        uint64_t __v;                                                                              \
        __asm__ volatile("mrs %0, " #name : "=r"(__v));                                            \
        __v;                                                                                       \
    })

/**
 * @brief Writes a system register with msr.
 *
 * @param name Register name as the assembler spells it, e.g. ttbr0_el1.
 * @param val  Value to write.
 */
#define WRITE_SYSREG(name, val)                                                                    \
    __asm__ volatile("msr " #name ", %0" ::"r"((uint64_t)(val)) : "memory")

#endif // ARCH_AARCH64_IMPL_SYSREG_H
