/**
 * @file svc_yield.c
 * @brief Implementation of the Yield() svc.
 */

#include "kernel/sched/sched.h"
#include "kernel/clock.h"
#include "svc.h"

void SvcYield(CpuState *frame)
{
    ArchSetInFrame(frame, 0, ZUZU_OK);
    if (!SchedAnyCpuTakers(current_task)) {
        current_task->slice_deadline = ArchTimerNow() + ClockUsToCounts(current_task->time_slice_us);
        return;
    }
        current_task->slice_deadline = 0;
    Schedule();
}
