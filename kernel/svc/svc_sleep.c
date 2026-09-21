#include "svc.h"
#include "kernel/sched/sched.h"
#include <arch/timer.h>


void SvcSleep(CpuState *frame)
{
    Duration ms = (*arch_reg(frame, 0)); // argument 0: Milliseconds to sleep

    current_thread->wake_deadline = ArchDeadlineFromMs(ms);
    current_thread->wake_reason = WAKE_NONE;

    // Change state to BLOCKED and insert into sleep queue
    current_thread->state = BLOCKED;
    SchedInsertSleepQueue(current_thread);
    // Schedule someone else immediately
    Schedule();

    (*arch_reg(frame, 0)) = 0;
}