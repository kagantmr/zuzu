/**
 * @file
 * @brief AArch64 frame-record stack walk.
 */

#ifndef ARCH_AARCH64_IMPL_BACKTRACE_H
#define ARCH_AARCH64_IMPL_BACKTRACE_H

#include <arch_impl/regs.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Follows the frame-record chain and collects return addresses.
 *
 * Under AAPCS64 x29 points at a 16-byte frame record: the caller's x29 at fp and the return
 * address (x30) at fp + 8. The walk stops at a NULL, misaligned or out-of-range fp, or one that
 * does not move up the stack.
 *
 * @param[in]  fp             Frame pointer (x29) to start from.
 * @param[in]  kernel_va_base Lowest address a valid frame may have.
 * @param[out] out            Return addresses, innermost first.
 * @param[in]  max_depth      Capacity of @p out.
 * @return The number of addresses stored.
 */
static inline size_t ArchBacktraceWalk(Register fp, Register kernel_va_base, uintptr_t *out,
                                       size_t max_depth)
{
    size_t depth = 0;
    uintptr_t cur = (uintptr_t)fp;
    const uintptr_t base = (uintptr_t)kernel_va_base;

    while (depth < max_depth) {
        if (cur == 0 || (cur & 0xFU) || cur < base)
            break;
        const uintptr_t *record = (const uintptr_t *)cur;
        uintptr_t prev = record[0];
        uintptr_t lr = record[1];
        if (lr == 0)
            break;
        out[depth++] = lr;
        if (prev <= cur)
            break;
        cur = prev;
    }
    return depth;
}

#endif // ARCH_AARCH64_IMPL_BACKTRACE_H
