#include "kernel/sched/sys_task.h"
#include "kernel/syscall/syscall.h"
#include "kernel/sched/sched.h"
#include "kernel/time/tick.h"
#include "kernel/proc/process.h"
#include "core/log.h"

extern process_t *current_process;

void sys_task_quit(exception_frame_t *frame) {
    current_process->process_state = PROCESS_ZOMBIE;
    current_process->exit_status = frame->r[0];
    
    // Defer destruction to the reaper (running in schedule())
    sched_defer_destroy(current_process); 
    KINFO("Process %d exited with status code %d", current_process->pid, current_process->exit_status);
    
    current_process = NULL;
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
        KWARN("Rejected bad pointer 0x%08X", (uint32_t)msg);
        frame->r[0] = ERR_PTRFAULT;
        return;
    }
    for (size_t i = 0; i < len; i++) {kprintf("%c", msg[i]);}
    frame->r[0] = 0; // Success
}

void sys_task_sleep(exception_frame_t *frame) {
    uint32_t ms = frame->r[0]; // Argument 0: Milliseconds to sleep
    
    // Convert ms to ticks (assuming 100Hz timer -> 10ms per tick)
    uint32_t ticks = ms / 10; 
    if (ticks == 0) ticks = 1; // Sleep at least 1 tick

    // Calculate wake time
    current_process->wake_tick = get_ticks() + ticks;
    
    // Change state
    current_process->process_state = PROCESS_BLOCKED;
    
    // Schedule someone else immediately
    schedule();
}

// Add this function
void sys_get_pid(exception_frame_t *frame) {
    frame->r[0] = current_process->pid;
}