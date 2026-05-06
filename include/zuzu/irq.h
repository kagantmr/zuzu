#ifndef ZUZU_IRQ_H
#define ZUZU_IRQ_H

#include "zuzu/syscall_nums.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- IRQ syscalls ---- */

static inline int32_t _irq_claim(uint32_t irq_num) {
    register uint32_t r0 __asm__("r0") = irq_num;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_IRQ_CLAIM)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _irq_bind(uint32_t irq_num, uint32_t ntfn_handle) {
    register uint32_t r0 __asm__("r0") = irq_num;
    register uint32_t r1 __asm__("r1") = ntfn_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_IRQ_BIND)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _irq_done(uint32_t irq_num) {
    register uint32_t r0 __asm__("r0") = irq_num;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_IRQ_DONE)
        : "memory");
    return (int32_t)r0;
}

#ifdef __cplusplus
}
#endif

#endif
