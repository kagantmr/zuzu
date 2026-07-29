/** ntfn.h -- zuzu kernel notification syscalls
 * 
 * Wraps the raw syscalls over a clean API.
 */

#ifndef ZUZU_NTFN_H
#define ZUZU_NTFN_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <arch/syscall.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t NtfnBits;  /* bitfield of pending signals */

/**
 * @brief Creates a new notification object and returns its handle.
 *
 * @return `Handle` Returns the handle of the newly created notification object on success, or a negative error code on failure.
 */
static inline Handle ZuzuNtfnCreate(void) {
    return Syscall(SYS_NTFN_CREATE, 0, 0, 0, 0);
}

/**
 * @brief Signals the specified notification object with the given bits.
 *
 * @param ntfn_handle The handle of the notification object to signal.
 * @param bits The bits to signal the notification object with.
 *
 * @return `Err` Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuNtfnSignal(Handle ntfn_handle, uint32_t bits) {
    return Syscall(SYS_NTFN_SIGNAL, ntfn_handle, bits, 0, 0);
}

/**
 * @brief Waits for the specified notification object to be signaled, with an optional timeout.
 *
 * @param ntfn_handle The handle of the notification object to wait on.
 * @param timeout_ms The timeout in milliseconds to wait for the notification. Use TIMEOUT_INFINITE for blocking indefinitely, or TIMEOUT_POLL for non-blocking.
 *
 * @return `NtfnBits` Returns the signaled bits on success, or a negative error code on failure. If the wait times out, returns ERR_TIMEOUT.
 */
static inline NtfnBits ZuzuNtfnBits(Handle ntfn_handle, Duration timeout_ms) {
    return Syscall(SYS_NTFN_WAIT, ntfn_handle, timeout_ms, 0, 0);
}

#ifdef __cplusplus
}
#endif

#endif
