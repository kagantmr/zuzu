#include "sys_task.h"
#include "kernel/syscall/syscall.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/vmm.h"
#include "kstack.h"
#include "arch/arm/mmu/mmu.h"
#include <mem.h>
#include "kernel/sched/sched.h"
#include "user/include/zuzu.h"
#include "kernel/loader/loader.h"
#include "kernel/time/tick.h"
#include "kernel/proc/process.h"
#include <spawn_args.h>

extern process_t *current_process;
extern list_head_t sleep_queue;
extern endpoint_t *nametable_endpoint;
extern process_t *process_table[MAX_PROCESSES];

#define LOG_FMT(fmt) "(sys_task) " fmt
#include "core/log.h"

#define WAIT_ANY_PID ((uint32_t)-1)

static bool wait_write_status(int32_t *status_out, int32_t status)
{
    if (!status_out)
        return true;

    return copy_to_user(status_out, &status, sizeof(status));
}

void quit(exception_frame_t *frame) {
    int exit_status = (int)frame->r[0];
    KINFO("Process %d exited with status code %d", current_process->pid, exit_status);
    
    process_kill(current_process, exit_status);
    schedule();
}

void yield(exception_frame_t *frame) {
    frame->r[0] = 0;
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
    current_process->wake_reason = WAKE_NONE;
    
    // Change state
    frame->r[0] = 0;
    current_process->process_state = PROCESS_BLOCKED;
    list_add_tail(&current_process->timeout_node, &sleep_queue.node);
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
            if (!wait_write_status(status_out, child->exit_status)) {
                frame->r[0] = ERR_PTRFAULT;
                return;
            }
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
        if (!wait_write_status(status_out, child->exit_status)) {
            frame->r[0] = ERR_PTRFAULT;
            return;
        }
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
        if (!wait_write_status(status_out, child->exit_status)) {
            frame->r[0] = ERR_PTRFAULT;
            return;
        }
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
    if (!wait_write_status(status_out, child->exit_status)) {
        frame->r[0] = ERR_PTRFAULT;
        return;
    }
    frame->r[0] = child->pid;
    process_destroy(child);
}

/* spawn syscall removed: use tspawn/kickstart with sysd */

void tspawn(exception_frame_t *frame) {
    const char* name = (const char *)frame->r[0];
    if (!validate_user_ptr((uintptr_t)name, 1)) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    char kname[64];
    if (!copy_from_user(kname, name, sizeof(kname) - 1)) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    kname[sizeof(kname) - 1] = '\0'; // Ensure null-termination

    process_t *process = process_create(kname);
    if (!process) {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    process_set_parent(process, current_process);

    // now return a handle 
    int slot = handle_vec_find_free(&current_process->handle_table);
    if (slot < 0) {
        process_destroy(process);
        frame->r[0] = ERR_NOMEM;
        return;
    }
    handle_vec_get(&current_process->handle_table, slot)->type = HANDLE_TASK;
    handle_vec_get(&current_process->handle_table, slot)->task = process;
    handle_vec_get(&current_process->handle_table, slot)->grantable = true;

    frame->r[0] = slot;
    frame->r[1] = process->pid;
    return;
}

void kickstart(exception_frame_t *frame) {
    kickstart_args_t *args = (kickstart_args_t *)frame->r[0];
    if (!validate_user_ptr((uintptr_t)args, sizeof(kickstart_args_t))) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    kickstart_args_t kargs;
    if (!copy_from_user(&kargs, args, sizeof(kickstart_args_t))) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, kargs.task_handle);
    if (!entry || entry->type != HANDLE_TASK) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    process_t *target = entry->task;
    if (!target || target->process_state != PROCESS_STOPPED) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    uintptr_t stack_top = target->kernel_stack_top;

    stack_top -= 17 * sizeof(uintptr_t); // make room for exception frame + r0-r1
    exception_frame_t *target_frame = (exception_frame_t *)stack_top;

    target_frame->r[0] = kargs.r0_val;
    target_frame->r[1] = kargs.r1_val;
    for (int i = 2; i < 13; i++) {
        target_frame->r[i] = 0;
    }
    target_frame->sp_usr = kargs.sp;
    target_frame->lr_usr = 0; // entry point for user code, set to
    target_frame->return_pc = kargs.entry;
    target_frame->return_cpsr = 0x10; // user mode, interrupts enabled

    stack_top -= sizeof(cpu_context_t);
    cpu_context_t *context = (cpu_context_t *)stack_top;
    memset(context, 0, sizeof(cpu_context_t));
    context->lr = (uint32_t)process_entry_trampoline;
    stack_top -= 132; // make room for VFP state
    memset((void *)stack_top, 0, 132);
    target->kernel_sp = (uint32_t *)stack_top;
    target->process_state = PROCESS_READY;
    sched_add(target);
    entry->type = HANDLE_FREE;
    entry->task = NULL;
    entry->grantable = false;
    frame->r[0] = 0;
    return;
}

void kill(exception_frame_t *frame) {
    uint32_t handle_idx = frame->r[0];

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_TASK) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    process_t *target = entry->task;
    if (!target || target->process_state != PROCESS_STOPPED) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    /* Unlink from parent's child list before destroying */
    if (target->sibling_node.prev && target->sibling_node.next)
        list_remove(&target->sibling_node);

    process_table[target->pid % MAX_PROCESSES] = NULL;
    if (target->as) {
        arch_mmu_free_user_pages(target->as);
        as_destroy(target->as);
    }
    handle_vec_destroy(&target->handle_table);
    if (target->kernel_stack_top)
        kstack_free(target->kernel_stack_top);
    kfree(target);

    entry->type = HANDLE_FREE;
    entry->task = NULL;
    entry->grantable = false;

    frame->r[0] = 0;
}