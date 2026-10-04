#include "kernel/sched/sched.h"
#include "svc.h"

void SvcYield(CpuState *frame)
{
    ArchSetInFrame(frame, 0, ZUZU_OK);
    Schedule();
}