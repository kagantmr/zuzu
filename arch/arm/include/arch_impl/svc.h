#ifndef ARCH_ARM_IMPL_SVC_H
#define ARCH_ARM_IMPL_SVC_H

#ifdef __thumb__
#define ARCH_SVC_MAGIC 0x23
#else
#define ARCH_SVC_MAGIC 0x112023
#endif

#ifndef __ASSEMBLER__

#include "regs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static __always_inline Register ArchInvokeSvc(uint32_t svc_num, Register a0, Register a1,
                                              Register a2, Register a3)
{
    register Register r0 __asm__("r0") = a0;
    register Register r1 __asm__("r1") = a1;
    register Register r2 __asm__("r2") = a2;
    register Register r3 __asm__("r3") = a3;
    register Register ip __asm__("r12") = (Register)svc_num;
    __asm__ volatile("svc %[magic]"
                     : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                     : "r"(ip), [magic] "i"(ARCH_SVC_MAGIC)
                     : "memory");
    return r0;
}

typedef struct {
    Register r0, r1, r2, r3;
} SvcResult;

static __always_inline SvcResult ArchInvokeSvc4(uint32_t svc_num, Register a0, Register a1,
                                                Register a2, Register a3)
{
    register Register r0 __asm__("r0") = a0;
    register Register r1 __asm__("r1") = a1;
    register Register r2 __asm__("r2") = a2;
    register Register r3 __asm__("r3") = a3;
    register Register ip __asm__("r12") = (Register)svc_num;
    __asm__ volatile("svc %[magic]"
                     : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                     : "r"(ip), [magic] "i"(ARCH_SVC_MAGIC)
                     : "memory");
    return (SvcResult){.r0 = r0, .r1 = r1, .r2 = r2, .r3 = r3};
}

#ifdef __cplusplus
}
#endif

#endif /* !__ASSEMBLER__ */

#endif /* ARCH_ARM_IMPL_SVC_H */
