#ifndef ARCH_ARM_IMPL_BACKTRACE_H
#define ARCH_ARM_IMPL_BACKTRACE_H

#include <arch_impl/regs.h>
#include <stddef.h>
#include <stdint.h>

static inline size_t ArchBacktraceWalk(Register fp, Register kernel_va_base, uint32_t *out,
                                       size_t max_depth)
{
    size_t depth = 0;
    uint32_t cur = (uint32_t)fp;
    uint32_t base = (uint32_t)kernel_va_base;

    while (depth < max_depth) {
        if (cur == 0 || (cur & 0x3u) || cur < base)
            break;
        uint32_t lr = *(uint32_t *)(uintptr_t)cur;
        out[depth++] = lr;
        uint32_t prev = *(uint32_t *)(uintptr_t)(cur - 4);
        if (prev <= cur)
            break;
        cur = prev;
    }
    return depth;
}

#endif // ARCH_ARM_IMPL_BACKTRACE_H
