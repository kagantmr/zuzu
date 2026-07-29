#ifndef ZUZU_IRQ_H
#define ZUZU_IRQ_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <arch/syscall.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- IRQ syscalls ---- */

/**
 * @brief Binds a device handle to a notification handle for interrupt handling.
 *
 * @param dev_handle The handle of the device to bind.
 * @param ntfn_handle The handle of the notification to bind to the device.
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuIrqBind(Handle dev_handle, Handle ntfn_handle) {
    return Syscall(SYS_IRQ_BIND, dev_handle, ntfn_handle, 0, 0);
}

/**
 * @brief Notifies the system that an interrupt has been handled for the specified device.
 *
 * @param dev_handle The handle of the device for which the interrupt has been handled.
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuIrqDone(Handle dev_handle) {
    return Syscall(SYS_IRQ_DONE, dev_handle, 0, 0, 0);
}

#ifdef __cplusplus
}
#endif

#endif
