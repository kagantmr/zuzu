#include "svc.h"
#include "kernel/sched/sched.h"
#include <arch/timer.h>


void SvcSleep(CpuState *frame)
{
    Duration duration_ms = (Duration)(*ArchGetFromFrame(frame, 0)); // argument 0: Milliseconds to sleep

    current_task->wake_deadline = ArchDeadlineFromMs(duration_ms);
    current_task->wake_reason = WAKE_NONE;

    // Change state to BLOCKED and insert into sleep queue
    current_task->state = BLOCKED;
    SchedInsertSleepQueue(current_task);
    // Schedule someone else immediately
    Schedule();

    ArchSetInFrame(frame, 0, ZUZU_OK);
}