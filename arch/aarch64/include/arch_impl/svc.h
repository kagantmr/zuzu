/**
 * @file
 * @brief AArch64 syscall entry: the svc instruction and its registers.
 *
 * The syscall number is the svc immediate, which the kernel reads from ESR_EL1.ISS[15:0], so
 * it must be a compile-time constant. Arguments go in x0-x3 and results come back in x0-x3.
 */

#ifndef ARCH_AARCH64_IMPL_SVC_H
#define ARCH_AARCH64_IMPL_SVC_H

#ifndef __ASSEMBLER__

#include "regs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Loads x0-x3 and issues svc #num; the shared body of ArchInvokeSvc and ArchInvokeSvc4.
 *
 * The arguments are evaluated into temporaries first: a function call among them would
 * otherwise clobber the x0-x3 register variables already loaded.
 */
#define ARCH_SVC_CALL(num, a0, a1, a2, a3)                                                         \
    Register a0_ = (Register)(a0);                                                                 \
    Register a1_ = (Register)(a1);                                                                 \
    Register a2_ = (Register)(a2);                                                                 \
    Register a3_ = (Register)(a3);                                                                 \
    register Register x0_ __asm__("x0") = a0_;                                                     \
    register Register x1_ __asm__("x1") = a1_;                                                     \
    register Register x2_ __asm__("x2") = a2_;                                                     \
    register Register x3_ __asm__("x3") = a3_;                                                     \
    __asm__ volatile("svc %[imm]"                                                                  \
                     : "+r"(x0_), "+r"(x1_), "+r"(x2_), "+r"(x3_)                                  \
                     : [imm] "i"(num)                                                              \
                     : "memory")

/**
 * @brief Makes a syscall and returns x0.
 *
 * @param num Syscall number, a compile-time constant; the svc immediate.
 * @param a0  Argument in x0.
 * @param a1  Argument in x1.
 * @param a2  Argument in x2.
 * @param a3  Argument in x3.
 * @return x0 after the call.
 */
#define ArchInvokeSvc(num, a0, a1, a2, a3)                                                         \
    ({                                                                                             \
        ARCH_SVC_CALL(num, a0, a1, a2, a3);                                                        \
        x0_;                                                                                       \
    })

/** @brief x0-x3 after a syscall. */
typedef struct {
    Register r0; /**< Status, or the primary result (x0). */
    Register r1; /**< Second result register (x1). */
    Register r2; /**< Third result register (x2). */
    Register r3; /**< Fourth result register (x3). */
} SvcResult;

/**
 * @brief Makes a syscall and returns x0-x3.
 *
 * @param num Syscall number, a compile-time constant; the svc immediate.
 * @param a0  Argument in x0.
 * @param a1  Argument in x1.
 * @param a2  Argument in x2.
 * @param a3  Argument in x3.
 * @return x0-x3 after the call, as an SvcResult.
 */
#define ArchInvokeSvc4(num, a0, a1, a2, a3)                                                        \
    ({                                                                                             \
        ARCH_SVC_CALL(num, a0, a1, a2, a3);                                                        \
        (SvcResult){.r0 = x0_, .r1 = x1_, .r2 = x2_, .r3 = x3_};                                   \
    })

#ifdef __cplusplus
}
#endif

#endif /* !__ASSEMBLER__ */

#endif /* ARCH_AARCH64_IMPL_SVC_H */
