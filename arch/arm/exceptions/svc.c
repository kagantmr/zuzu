#include "kernel/svc/svc.h"
#include <arch/regs.h>
#include <compiler.h>

void __hot SvcTrap(CpuState *frame);

void __hot SvcTrap(CpuState *frame)
{
    if (unlikely((frame->return_cpsr & 0x1F) != 0x10))
        return;
    SvcDispatch(ArchFrameSvcNum(frame), frame);
}
