#ifndef ZUZU_IRQ_H
#define ZUZU_IRQ_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
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
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_irq_bind(handle_t dev_handle, handle_t ntfn_handle) {
    register handle_t r0 __asm__("r0") = dev_handle;
    register handle_t r1 __asm__("r1") = ntfn_handle;
    __asm__ volatile("svc %[num]"
        : "+r"(r0)
        : "r"(r1), [num] "i"(SYS_IRQ_BIND)
        : "memory");
    return (int32_t)r0;
}

/**
 * @brief Notifies the system that an interrupt has been handled for the specified device.
 * 
 * @param dev_handle The handle of the device for which the interrupt has been handled.
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_irq_done(handle_t dev_handle) {
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
