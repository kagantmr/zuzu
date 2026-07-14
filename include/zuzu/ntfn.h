#ifndef ZUZU_NTFN_H
#define ZUZU_NTFN_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Notification syscalls ---- */

static inline int32_t zuzu_ntfn_create(void) {
    register uint32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_NTFN_CREATE)
        : "memory");
    return (int32_t)r0;
}

/* (handle, bits) -> 0 or -err; bits are 31-bit — bit 31 is rejected with
 * ERR_BADARG because delivered bits ride in r0 where negatives mean errors */
static inline int32_t zuzu_ntfn_signal(handle_t ntfn_handle, uint32_t bits) {
    register handle_t r0 __asm__("r0") = ntfn_handle;
    register uint32_t r1 __asm__("r1") = bits;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_NTFN_SIGNAL)
        : "memory");
    return (int32_t)r0;
}

/* (handle, timeout_ms) -> bits or -err; TIMEOUT_POLL polls, TIMEOUT_INFINITE blocks */
static inline int32_t zuzu_ntfn_wait(handle_t ntfn_handle, uint32_t timeout_ms) {
    register handle_t r0 __asm__("r0") = ntfn_handle;
    register uint32_t r1 __asm__("r1") = timeout_ms;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_NTFN_WAIT)
        : "memory");
    return (int32_t)r0;
}

#ifdef __cplusplus
}
#endif

#endif
