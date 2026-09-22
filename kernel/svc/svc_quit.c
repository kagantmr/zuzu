#include "svc.h"
#include "kernel/proc/process.h"
#include "kernel/sched/sched.h"

#define LOG_FMT(fmt) "(SvcQuit) " fmt
#include <zuzu/log.h>

void SvcQuit(CpuState *frame)
{
    int exit_status = (int)(*arch_reg(frame, 0));

    SpaceObject *owner = current_thread->owner_process;
    current_thread->exit_status = exit_status;
    WakeJoinTask(current_thread, exit_status);

    if (list_one_elem(&owner->threads)) {
        // last thread, kill the process
        ProcessKill(owner, exit_status);
    } else {
        KillTask(current_thread);
        // remove from process thread list NOW so process_destroy won't see it
        if (current_thread->process_node.prev && current_thread->process_node.next)
            list_remove(&current_thread->process_node);
        SchedQueueDestroyThread(current_thread);
    }

    Schedule();
}