#ifndef ZUZU_IRQ_H
#define ZUZU_IRQ_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- IRQ syscalls ---- */

static inline int32_t _irq_claim(handle_t dev_handle) {
    register handle_t r0 __asm__("r0") = dev_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_IRQ_CLAIM)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _irq_bind(handle_t dev_handle, handle_t ntfn_handle) {
    register handle_t r0 __asm__("r0") = dev_handle;
    register handle_t r1 __asm__("r1") = ntfn_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_IRQ_BIND)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _irq_done(handle_t dev_handle) {
    register handle_t r0 __asm__("r0") = dev_handle;
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
