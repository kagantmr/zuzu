#ifndef ARCH_ARM_IMPL_TLS_H
#define ARCH_ARM_IMPL_TLS_H

#include <stdint.h>

static inline uintptr_t ArchGetTlsPointer(void)
{
    uintptr_t tp;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tp));
    return tp;
}

#endif // ARCH_ARM_IMPL_TLS_H
