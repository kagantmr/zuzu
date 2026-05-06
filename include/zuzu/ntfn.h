#ifndef ZUZU_NTFN_H
#define ZUZU_NTFN_H

#include "zuzu/syscall_nums.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Notification syscalls ---- */

static inline int32_t _ntfn_create(void) {
    register uint32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_NTFN_CREATE)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _ntfn_signal(uint32_t ntfn_handle, uint32_t bits) {
    register uint32_t r0 __asm__("r0") = ntfn_handle;
    register uint32_t r1 __asm__("r1") = bits;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_NTFN_SIGNAL)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _ntfn_wait(uint32_t ntfn_handle) {
    register uint32_t r0 __asm__("r0") = ntfn_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_NTFN_WAIT)
        : "memory");
    return (int32_t)r0;
}

static inline int32_t _ntfn_poll(uint32_t ntfn_handle) {
    register uint32_t r0 __asm__("r0") = ntfn_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : [num] "i"(SYS_NTFN_POLL)
        : "memory");
    return (int32_t)r0;
}

#ifdef __cplusplus
}
#endif

#endif
