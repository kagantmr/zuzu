#include "sys_task.h"
#include "kernel/syscall/syscall.h"
#include <mem.h>
#include "kernel/sched/sched.h"
#include "user/include/zuzu.h"
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
    uint32_t ms = frame->r[0]; // argument 0: Milliseconds to sleep
    
    // Convert ms to ticks (assuming 100Hz timer -> 10ms per tick)
    uint32_t ticks = ms / 10; 
    if (ticks == 0) ticks = 1; // Sleep at least 1 tick

    // Calculate wake time
    current_process->wake_tick = get_ticks() + ticks;
    
    // Change state
    current_process->process_state = PROCESS_BLOCKED;
    list_add_tail(&current_process->node, &sleep_queue.node);
    // Schedule someone else immediately
    schedule();
}

// Add this function
void get_pid(exception_frame_t *frame) {
    frame->r[0] = current_process->pid;
}

void wait(exception_frame_t *frame) {
    uint32_t child_pid = frame->r[0];
    int32_t *status_out = (int32_t *)frame->r[1];
    uint32_t flags = frame->r[2];

    process_t *child = process_find_by_pid(child_pid);
    if (!child) {
        frame->r[0] = ERR_NOENT;
        return;
    }

    if (child->parent_pid != current_process->pid) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    // Case A: child already exited
    if (child->process_state == PROCESS_ZOMBIE) {
        if (status_out && validate_user_ptr((uintptr_t)status_out, sizeof(int32_t)))
            *status_out = child->exit_status;
        frame->r[0] = child->exit_status;
        process_destroy(child);
        return;
    }

    // Case B: child still running, non-blocking
    if (flags & WNOHANG) {
        frame->r[0] = 0;
        return;
    }

    // Case C: block until child exits
    current_process->waiting_for = child_pid;
    current_process->process_state = PROCESS_BLOCKED;
    schedule();

    // re-fetch after wakeup, pointer may be stale
    child = process_find_by_pid(child_pid);
    if (!child) {
        frame->r[0] = ERR_NOENT;
        return;
    }
    if (status_out && validate_user_ptr((uintptr_t)status_out, sizeof(int32_t)))
        *status_out = child->exit_status;
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
        frame->r[0] = ERR_BADARG;
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
    process_t *process = process_create_from_elf(elf_data, elf_size, kname);
    if (!process) {
        frame->r[0] = ERR_NOMEM;
        return;
    }
    
    if (nametable_endpoint != NULL) {
        handle_entry_t *nt_entry = handle_vec_get(&process->handle_table, 0);
        if (nt_entry) {
            nt_entry->ep = nametable_endpoint;
            nt_entry->grantable = true;
            nt_entry->type = HANDLE_ENDPOINT;
        }
    }

    process->parent_pid = current_process->pid;
    
    sched_add(process);
    frame->r[0] = process->pid;
}