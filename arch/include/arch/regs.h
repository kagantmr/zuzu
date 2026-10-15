#ifndef ARCH_REGS_H
#define ARCH_REGS_H

#include <arch_impl/regs.h>

static __always_inline void ArchSetInFrame(CpuState *f, unsigned i, Register value)
{
    *ArchGetFromFrame(f, i) = value;
}

#endif // ARCH_REGS_H
