/**
 * @file
 * @brief Architecture-neutral access to saved register frames.
 */

#ifndef ARCH_REGS_H
#define ARCH_REGS_H

#include <arch_impl/regs.h>

/**
 * @brief Overwrites argument/result register @p i of a saved frame.
 *
 * @param[in,out] f     Frame to change.
 * @param[in]     i     Register index, as for ArchGetFromFrame().
 * @param[in]     value New value.
 */
static __always_inline void ArchSetInFrame(CpuState *f, unsigned i, Register value)
{
    *ArchGetFromFrame(f, i) = value;
}

#endif // ARCH_REGS_H
