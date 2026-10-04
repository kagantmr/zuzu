#include "kernel/sched/sched.h"
#include "svc.h"
#include <arch/timer.h>

void SvcSleep(CpuState *frame)
{
    Duration duration_ms =
        (Duration)(*ArchGetFromFrame(frame, 0)); // argument 0: Milliseconds to sleep

    current_task->wake_deadline = ArchDeadlineFromMs(duration_ms);

    // Change state to TASK_STATE_BLOCKED and insert into sleep queue
    current_task->state = TASK_STATE_BLOCKED;
    SchedInsertSleepQueue(current_task);
    // Schedule someone else immediately
    Schedule();

    ArchSetInFrame(frame, 0, ZUZU_OK);
}