#include "sys_task.h"
#include "syscall.h"
#include "kernel/sched/sched.h"
#include "kernel/proc/process.h"
#include "core/kprintf.h"

extern process_t *current_process;

void sys_task_quit(exception_frame_t *frame) {
    current_process->process_state = PROCESS_ZOMBIE;
    current_process->exit_status = frame->r[0];
    // process_t *dying = current_process;
    // todo: cleanup in waitpid (phase 20)
    current_process = NULL;  // so schedule() doesn't re-enqueue it
    schedule();
}
void sys_task_yield(exception_frame_t *frame) {
    (void)frame;
    schedule();
}

void sys_log(exception_frame_t *frame) {
    const char* msg = (const char *)frame->r[0];
    size_t len = frame->r[1];
    if (!validate_user_ptr((uintptr_t)msg, len)) {
        kprintf("Rejected bad pointer 0x%08x\n", (uint32_t)msg);
        frame->r[0] = ERR_PTRFAULT;
        return;
    }
    for (size_t i = 0; i < len; i++) {kprintf("%c", msg[i]);}
    frame->r[0] = 0; // Success
}