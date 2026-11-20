/**
 * @file svc_sleep.c
 * @brief Implementation of the Sleep() svc.
 */

#include "kernel/sched/sched.h"
#include "svc.h"
#include <arch/timer.h>

void SvcSleep(CpuState *frame)
{
    Duration duration_ms =
        (Duration)(*ArchGetFromFrame(frame, 0));

    current_task->wake_deadline = ArchDeadlineFromMs(duration_ms);

    current_task->state = TASK_STATE_BLOCKED;
    SchedInsertSleepQueue(current_task);
    current_task->timeout_rc = ZUZU_OK;
    Schedule();
}
