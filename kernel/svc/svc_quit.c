#include "svc.h"
#include "kernel/proc/process.h"
#include "kernel/sched/sched.h"

#define LOG_FMT(fmt) "(SvcQuit) " fmt
#include <zuzu/log.h>


void SvcQuit(CpuState *frame)
{
    int exit_status = (int)(*arch_reg(frame, 0));
    int scope = (int)(*arch_reg(frame, 1));

    if (scope == SELF_THREAD) {
            ProcessObj *owner = current_thread->owner_process;
            current_thread->exit_status = exit_status;
            ThreadWakeJoiners(current_thread, exit_status);

            if (list_one_elem(&owner->threads)) {
                // last thread, kill the process
                ProcessKill(owner, exit_status);
            } else {
                ThreadKill(current_thread);
                // remove from process thread list NOW so process_destroy won't see it
                if (current_thread->process_node.prev && current_thread->process_node.next)
                    list_remove(&current_thread->process_node);
                SchedQueueDestroyThread(current_thread);
            }
        } else {
        // do NOT return an error, anything that isnt "kill thread" is accepted as kill process.
        // eases backwards compatibility, apps from Loaf can call this without any compat shim.
        KDEBUG("Task %d exited with status code %d",
            current_thread->owner_process ? current_thread->owner_process->pid : 0, exit_status);

        ProcessKill(current_thread->owner_process, exit_status);
    }

	Schedule();
}