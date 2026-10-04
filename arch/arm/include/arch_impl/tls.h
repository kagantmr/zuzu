// arch_impl/tls.h - ARM user-mode thread-pointer read (architecture-private).
//
// Do not include directly from neutral code; include <arch/tls.h> instead.

#ifndef ARCH_ARM_IMPL_TLS_H
#define ARCH_ARM_IMPL_TLS_H

#include <stdint.h>

/**
 * Reads the ARM TPIDRURO register (cp15, c13, c0, 3), which the kernel
 * fills in via ArchSetTlsPointer() on context switch. User mode uses
 * this for TLS / TCB lookup.
 */
static inline uintptr_t ArchGetTlsPointer(void)
{
    uintptr_t tp;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tp));
    return tp;
}

#endif // ARCH_ARM_IMPL_TLS_H
