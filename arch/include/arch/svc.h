#ifndef ARCH_SVC_H
#define ARCH_SVC_H

#include "regs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic svc dispatcher. svc_num must be a compile-time constant
 * (always_inline ensures constant propagation satisfies the "i" constraint).
 */
static __attribute__((always_inline)) inline int32_t
ArchInvokeSvc(uint32_t svc_num, Register a0, Register a1, Register a2, Register a3)
{
    register Register r0 __asm__("r0") = a0;
    register Register r1 __asm__("r1") = a1;
    register Register r2 __asm__("r2") = a2;
    register Register r3 __asm__("r3") = a3;
    __asm__ volatile("svc %[num]"
                     : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                     : [num] "i"(svc_num)
                     : "memory");
    return (int32_t)r0;
}

typedef struct {
    Register r0, r1, r2, r3;
} SvcResult;

static __attribute__((always_inline)) inline SvcResult
ArchInvokeSvc4(uint32_t svc_num, Register a0, Register a1, Register a2, Register a3)
{
    register Register r0 __asm__("r0") = a0;
    register Register r1 __asm__("r1") = a1;
    register Register r2 __asm__("r2") = a2;
    register Register r3 __asm__("r3") = a3;
    __asm__ volatile("svc %[num]"
                     : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                     : [num] "i"(svc_num)
                     : "memory");
    return (SvcResult){.r0 = r0, .r1 = r1, .r2 = r2, .r3 = r3};
}

#ifdef __cplusplus
}
#endif

#endif /* ARCH_SVC_H */
