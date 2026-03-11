#include "sys_task.h"
#include "kernel/syscall/syscall.h"
#include <mem.h>
#include "kernel/sched/sched.h"
#include "kernel/loader/initrd.h"
#include "kernel/loader/loader.h"
#include "kernel/time/tick.h"
#include "kernel/proc/process.h"

extern process_t *current_process;
extern list_head_t sleep_queue;
extern endpoint_t *nametable_endpoint;

#define LOG_FMT(fmt) "(sys_task) " fmt
#include "core/log.h"

void quit(exception_frame_t *frame) {
    KINFO("Process %d exited with status code %d", current_process->pid, current_process->exit_status);
    
    process_kill(current_process, frame->r[0]);
    schedule();
}

void yield(exception_frame_t *frame) {
    (void)frame;
    schedule();
}

void sleep(exception_frame_t *frame) {
    uint32_t ms = frame->r[0]; // Argument 0: Milliseconds to sleep
    
    // Convert ms to ticks (assuming 100Hz timer -> 10ms per tick)
    uint32_t ticks = ms / 10; 
    if (ticks == 0) ticks = 1; // Sleep at least 1 tick

    // Calculate wake time
    current_process->wake_tick = get_ticks() + ticks;
    
    // Change state
    current_process->process_state = PROCESS_BLOCKED;
    list_add_tail(&current_process->node, &sleep_queue.node);
    //KDEBUG("Woke up as process PID %d",current_process->pid);
    // Schedule someone else immediately
    schedule();
}

// Add this function
void get_pid(exception_frame_t *frame) {
    frame->r[0] = current_process->pid;
}

void wait(exception_frame_t *frame) {
    uint32_t child_pid = frame->r[0];

    // find the child
    process_t *child = process_find_by_pid(child_pid);
    if (!child) {
        frame->r[0] = -ERR_NOENT;
        return;
    }

    // verify we're the parent
    if (child->parent_pid != current_process->pid) {
        frame->r[0] = -ERR_BADARG;
        return;
    }

    // Case A: child already exited
    if (child->process_state == PROCESS_ZOMBIE) {
        frame->r[0] = child->exit_status;
        process_destroy(child);
        return;
    }

    // Case B: child still running — block ourselves
    current_process->waiting_for = child_pid;
    current_process->process_state = PROCESS_BLOCKED;
    schedule();
    
    // we wake up here after child exits
    frame->r[0] = child->exit_status;
    process_destroy(child);
}

void spawn(exception_frame_t *frame) {
    const char *name = (const char *)frame->r[0];
    size_t name_len = frame->r[1];
    if (!validate_user_ptr((uintptr_t)name, name_len)) {
        frame->r[0] = ERR_BADARG;
        return;
    }
    char kname[64];
    if (name_len >= sizeof(kname)) {
        frame->r[0] = -ERR_BADARG;
        return;
    }
    memcpy(kname, name, name_len);
    kname[name_len] = '\0';
    const void *elf_data;
    size_t elf_size;
    if (!initrd_find(kname, &elf_data, &elf_size)) {
        frame->r[0] = ERR_NOENT;
        return;
    }
    process_t *process = process_create_from_elf(elf_data, elf_size, name);
    if (!process) {
        frame->r[0] = -ERR_NOMEM;
        return;
    }
    
    if (nametable_endpoint != NULL) {
        process->handle_table[0].ep = nametable_endpoint;
        process->handle_table[0].grantable = true;
        process->handle_table[0].type = HANDLE_ENDPOINT;
    }

    process->parent_pid = current_process->pid;
    
    sched_add(process);
    frame->r[0] = process->pid;
}

void sys_log(exception_frame_t *frame) {
    const char* msg = (const char *)frame->r[0];
    
    size_t len = frame->r[1];
    if (!validate_user_ptr((uintptr_t)msg, len)) {
        //KDEBUG("Rejected bad pointer 0x%08X", (uint32_t)msg);
        frame->r[0] = ERR_PTRFAULT;
        return;
    }

    kprintf("%.*s", (int)len, msg);
    frame->r[0] = 0; // Success
}