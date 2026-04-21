#include "sys_task.h"
#include "kernel/syscall/syscall.h"
#include "kernel/mm/alloc.h"
#include <mem.h>
#include "kernel/sched/sched.h"
#include "user/include/zuzu.h"
#include "kernel/loader/loader.h"
#include "kernel/time/tick.h"
#include "kernel/proc/process.h"

extern process_t *current_process;
extern list_head_t sleep_queue;
extern endpoint_t *nametable_endpoint;

#define LOG_FMT(fmt) "(sys_task) " fmt
#include "core/log.h"

#define WAIT_ANY_PID ((uint32_t)-1)

static void wait_write_status(int32_t *status_out, int32_t status)
{
    if (!status_out)
        return;

    (void)copy_to_user(status_out, &status, sizeof(status));
}

void quit(exception_frame_t *frame) {
    int exit_status = (int)frame->r[0];
    KINFO("Process %d exited with status code %d", current_process->pid, exit_status);
    
    process_kill(current_process, exit_status);
    schedule();
}

void yield(exception_frame_t *frame) {
    (void)frame;
    schedule();
}

void sleep(exception_frame_t *frame) {
    uint32_t ms = frame->r[0]; // argument 0: Milliseconds to sleep
    
    // Convert ms to ticks using configured tick rate.
    uint64_t ticks = ((uint64_t)ms * (uint64_t)TICK_HZ) / 1000u;
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
    int32_t req_pid = (int32_t)frame->r[0];
    int32_t *status_out = (int32_t *)frame->r[1];
    uint32_t flags = frame->r[2];
    process_t *child = NULL;

    if (req_pid == -1) {
        child = process_find_zombie_child(current_process);
        if (child) {
            wait_write_status(status_out, child->exit_status);
            frame->r[0] = child->pid;
            process_destroy(child);
            return;
        }

        if (flags & WNOHANG) {
            frame->r[0] = 0;
            return;
        }

        current_process->waiting_for = WAIT_ANY_PID;
        current_process->process_state = PROCESS_BLOCKED;
        schedule();

        child = process_find_zombie_child(current_process);
        if (!child) {
            frame->r[0] = ERR_NOENT;
            return;
        }
        wait_write_status(status_out, child->exit_status);
        frame->r[0] = child->pid;
        process_destroy(child);
        return;
    }

    if (req_pid < 0) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    uint32_t child_pid = (uint32_t)req_pid;
    child = process_find_child_by_pid(current_process, child_pid);
    if (!child) {
        frame->r[0] = ERR_NOENT;
        return;
    }

    // Case A: child already exited
    if (child->process_state == PROCESS_ZOMBIE) {
        wait_write_status(status_out, child->exit_status);
        frame->r[0] = child->pid;
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
    child = process_find_child_by_pid(current_process, child_pid);
    if (!child) {
        frame->r[0] = ERR_NOENT;
        return;
    }
    wait_write_status(status_out, child->exit_status);
    frame->r[0] = child->pid;
    process_destroy(child);
}

void spawn(exception_frame_t *frame) {
    const void *elf_data = (const void *)frame->r[0];
    size_t elf_size = frame->r[1];
    const char *name = (const char *)frame->r[2];
    size_t name_len = frame->r[3];

    if (!elf_data || !name || elf_size == 0 || name_len == 0) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    char kname[64];
    if (name_len >= sizeof(kname)) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    void *elf_copy = kmalloc(elf_size);
    if (!elf_copy) {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    if (!copy_from_user(elf_copy, elf_data, elf_size)) {
        kfree(elf_copy);
        frame->r[0] = ERR_BADARG;
        return;
    }

    if (!copy_from_user(kname, name, name_len)) {
        kfree(elf_copy);
        frame->r[0] = ERR_BADARG;
        return;
    }
    kname[name_len] = '\0';

    process_t *process = process_create_from_elf(elf_copy, elf_size, kname);
    kfree(elf_copy);
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

    process_set_parent(process, current_process);
    
    sched_add(process);
    frame->r[0] = process->pid;
}
