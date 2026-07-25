// arch_impl/tls.h - ARM user-mode thread-pointer read (architecture-private).
//
// Do not include directly from neutral code; include <arch/tls.h> instead.

#ifndef ZUZU_ARM_IMPL_TLS_H
#define ZUZU_ARM_IMPL_TLS_H

#include <stdint.h>

/**
 * arch_get_thread_ptr - read the thread pointer published by the kernel.
 *
 * Reads the ARM TPIDRURO register (cp15, c13, c0, 3), which the kernel
 * fills in via arch_set_thread_ptr() on context switch. User mode uses
 * this for TLS / TCB lookup.
 */
static inline uintptr_t arch_get_thread_ptr(void)
{
    uintptr_t tp;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tp));
    return tp;
}

#endif // ZUZU_ARM_IMPL_TLS_H
