#ifndef ZUZU_SYSCALL_H
#define ZUZU_SYSCALL_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic syscall dispatcher. svc_num must be a compile-time constant
 * (always_inline ensures constant propagation satisfies the "i" constraint).
 * Returns r0. Use syscall_msg() when r1-r3 are also output registers.
 */
static __attribute__((always_inline)) inline
int32_t syscall(uint32_t svc_num, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), [num] "i"(svc_num)
        : "memory");
    return (int32_t)r0;
}

/* Like syscall() but returns all four output registers as msg_t. */
static __attribute__((always_inline)) inline
msg_t syscall_msg(uint32_t svc_num, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    __asm__ volatile("svc %[num]"
        : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
        : [num] "i"(svc_num)
        : "memory");
    return (msg_t){.r0 = r0, .r1 = r1, .r2 = r2, .r3 = r3};
}

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_SYSCALL_H */
