#include "svc.h"
#include "kernel/sched/sched.h"

void SvcYield(CpuState *frame)
{
    ArchSetInFrame(frame, 0, ZUZU_OK);
    Schedule();
}