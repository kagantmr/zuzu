#ifndef ZUZU_NTFN_H
#define ZUZU_NTFN_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <arch/syscall.h>
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
    return syscall(SYS_NTFN_CREATE, 0, 0, 0, 0);
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
    return syscall(SYS_NTFN_SIGNAL, ntfn_handle, bits, 0, 0);
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
    return syscall(SYS_NTFN_WAIT, ntfn_handle, timeout_ms, 0, 0);
}

#ifdef __cplusplus
}
#endif

#endif
