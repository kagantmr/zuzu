#include "svc.h"
#include "kernel/sched/sched.h"

void SvcYield(CpuState *frame)
{
    (*arch_reg(frame, 0)) = 0;
    (void)frame;
    Schedule();
}