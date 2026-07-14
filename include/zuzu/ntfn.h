#ifndef ZUZU_NTFN_H
#define ZUZU_NTFN_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Notification syscalls ---- */

/**
 * @brief Creates a new notification object and returns its handle.
 * 
 * @return int32_t Returns the handle of the newly created notification object on success, or a negative error code on failure.
 */
static inline int32_t zuzu_ntfn_create(void) {
    register uint32_t r0 __asm__("r0");
    __asm__ volatile("svc %[num]"
        : "=r"(r0)
        : [num] "i"(SYS_NTFN_CREATE)
        : "memory");
    return (int32_t)r0;
}

/**
 * @brief Signals the specified notification object with the given bits.
 * 
 * @param ntfn_handle The handle of the notification object to signal.
 * @param bits The bits to signal the notification object with.
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_ntfn_signal(handle_t ntfn_handle, uint32_t bits) {
    register handle_t r0 __asm__("r0") = ntfn_handle;
    register uint32_t r1 __asm__("r1") = bits;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_NTFN_SIGNAL)
        : "memory");
    return (int32_t)r0;
}

/**
 * @brief Waits for the specified notification object to be signaled, with an optional timeout.
 * 
 * @param ntfn_handle The handle of the notification object to wait on.
 * @param timeout_ms The timeout in milliseconds to wait for the notification. Use TIMEOUT_INFINITE for blocking indefinitely, or TIMEOUT_POLL for non-blocking.
 * 
 * @return int32_t Returns the signaled bits on success, or a negative error code on failure. If the wait times out, returns ERR_TIMEOUT.
 */
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
