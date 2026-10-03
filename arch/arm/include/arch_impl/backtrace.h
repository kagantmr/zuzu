// arch_impl/backtrace.h - ARM (AAPCS) frame-pointer stack walk.
//
// Do not include directly from neutral code; include <arch/backtrace.h>
// instead.

#ifndef ZUZU_ARM_IMPL_BACKTRACE_H
#define ZUZU_ARM_IMPL_BACKTRACE_H

#include <arch_impl/regs.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Walk the AAPCS frame-pointer chain starting at fp, collecting return
 * addresses into out[]. GCC's ARM prologue stores [fp] = saved lr,
 * [fp-4] = saved fp, so each step is a pair of loads.
 *
 * @param fp              Starting frame pointer (r11): live, or captured
 *                         from a fault frame.
 * @param kernel_va_base  Lowest VA considered a plausible kernel frame
 *                         pointer; below this, the chain is treated as
 *                         exhausted rather than followed into garbage.
 * @param out             Destination array for collected return addresses.
 * @param max_depth       Capacity of out[].
 * @return Number of addresses written to out[].
 */
static inline size_t arch_backtrace_walk(Register fp, Register kernel_va_base, uint32_t *out,
                                          size_t max_depth)
{
    size_t depth = 0;
    uint32_t cur = (uint32_t)fp;
    uint32_t base = (uint32_t)kernel_va_base;

    while (depth < max_depth)
    {
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

#endif // ZUZU_ARM_IMPL_BACKTRACE_H
