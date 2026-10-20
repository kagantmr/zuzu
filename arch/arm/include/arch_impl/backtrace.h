/**
 * @file
 * @brief ARMv7-A frame-pointer stack walk.
 */

#ifndef ARCH_ARM_IMPL_BACKTRACE_H
#define ARCH_ARM_IMPL_BACKTRACE_H

#include <arch_impl/regs.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Follows the frame-pointer chain and collects return addresses.
 *
 * Each frame holds the saved lr at fp and the caller's fp at fp - 4. The walk
 * stops at a NULL, misaligned or out-of-range fp, or one that does not move up
 * the stack.
 *
 * @param[in]  fp             Frame pointer to start from.
 * @param[in]  kernel_va_base Lowest address a valid frame may have.
 * @param[out] out            Return addresses, innermost first.
 * @param[in]  max_depth      Capacity of @p out.
 * @return The number of addresses stored.
 */
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
