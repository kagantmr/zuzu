#include <arch/context.h>
#include "sys_proc.h"
#include "kernel/syscall/syscall.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/vmm.h"
#include "kstack.h"
#include <arch/mmu.h>
#include "kernel/mm/pmm.h"
#include <mem.h>
#include "kernel/sched/sched.h"
#include "zuzu/zuzu.h"
#include "kernel/time/tick.h"
#include "kernel/proc/process.h"
#include <zuzu/user_layout.h>
#include <zuzu/tcb.h>
#include <spawn_args.h>


extern thread_t *current_thread;
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

void sys_pquit(arch_regs_t *frame) {
    int exit_status = (int)(*arch_reg(frame, 0));
    KDEBUG("Process %d exited with status code %d", 
           current_thread->owner_process ? current_thread->owner_process->pid : 0, 
           exit_status);
    
    process_kill(current_thread->owner_process, exit_status);
    schedule();
}

void sys_yield(arch_regs_t *frame) {
    (*arch_reg(frame, 0)) = 0;
    (void)frame;
    schedule();
}

void sys_sleep(arch_regs_t *frame) {
    uint32_t ms = (*arch_reg(frame, 0)); // argument 0: Milliseconds to sleep
    
    // Convert ms to ticks using configured tick rate.
    uint64_t ticks = ((uint64_t)ms * (uint64_t)TICK_HZ) / 1000u;
    if (ticks == 0) ticks = 1; // Sleep at least 1 tick

    // Calculate wake time
    current_thread->wake_tick = get_ticks() + ticks;
    current_thread->wake_reason = WAKE_NONE;
    
    // Change state to BLOCKED and insert into sleep queue
    current_thread->state = BLOCKED;
    sleep_queue_insert(current_thread);
    // Schedule someone else immediately
    schedule();

    (*arch_reg(frame, 0)) = 0;
}

void sys_getpid(arch_regs_t *frame) {
    (*arch_reg(frame, 0)) = current_thread->owner_process->pid;
}

void sys_wait(arch_regs_t *frame) {
    int32_t req_pid = (int32_t)(*arch_reg(frame, 0));
    int32_t *status_out = (int32_t *)(*arch_reg(frame, 1));
    uint32_t flags = (*arch_reg(frame, 2));
    process_t *child = NULL;

    if (req_pid == -1) {
        child = process_find_zombie_child(current_thread->owner_process);
        if (child) {
            if (!wait_write_status(status_out, child->exit_status)) {
                (*arch_reg(frame, 0)) = ERR_BADPTR;
                return;
            }
            (*arch_reg(frame, 0)) = child->pid;
            process_destroy(child);
            return;
        }

        if (flags & WNOHANG) {
            (*arch_reg(frame, 0)) = 0;
            return;
        }

        current_thread->owner_process->waiting_for = WAIT_ANY_PID;
        current_thread->state = BLOCKED;
        schedule();

        child = process_find_zombie_child(current_thread->owner_process);
        if (!child) {
            (*arch_reg(frame, 0)) = ERR_NOENT;
            return;
        }
        if (!wait_write_status(status_out, child->exit_status)) {
            (*arch_reg(frame, 0)) = ERR_BADPTR;
            return;
        }
        (*arch_reg(frame, 0)) = child->pid;
        process_destroy(child);
        return;
    }

    if (req_pid < 0) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    uint32_t child_pid = (uint32_t)req_pid;
    child = process_find_child_by_pid(current_thread->owner_process, child_pid);
    if (!child) {
        (*arch_reg(frame, 0)) = ERR_NOENT;
        return; 
    }

    // Case A: child already exited
    if (child->thread->state == ZOMBIE) {
        if (!wait_write_status(status_out, child->exit_status)) {
            (*arch_reg(frame, 0)) = ERR_BADPTR;
            return;
        }
        (*arch_reg(frame, 0)) = child->pid;
        process_destroy(child);
        return;
    }

    // Case B: child still running, non-blocking
    if (flags & WNOHANG) {
        (*arch_reg(frame, 0)) = 0;
        return;
    }

    // Case C: block until child exits
    current_thread->owner_process->waiting_for = child_pid;
    current_thread->state = BLOCKED;
    schedule();

    // re-fetch after wakeup, pointer may be stale
    child = process_find_child_by_pid(current_thread->owner_process, child_pid);
    if (!child) {
        (*arch_reg(frame, 0)) = ERR_NOENT;
        return;
    }
    if (!wait_write_status(status_out, child->exit_status)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }
    (*arch_reg(frame, 0)) = child->pid;
    process_destroy(child);
}

/* spawn syscall removed: use pspawn/kickstart with sysd */

void sys_pspawn(arch_regs_t *frame) {
    spawn_args_t *args = (spawn_args_t *)(*arch_reg(frame, 0));
    if (!validate_user_ptr((uintptr_t)args, sizeof(spawn_args_t))) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    spawn_args_t kargs;
    if (!copy_from_user(&kargs, args, sizeof(spawn_args_t))) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    if (kargs.size < sizeof(spawn_args_t)) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    if (!validate_user_ptr((uintptr_t)kargs.name, 1)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    char kname[64];
    size_t nlen = kargs.name_len;
    if (nlen > sizeof(kname) - 1)
        nlen = sizeof(kname) - 1;
    if (nlen > 0 && !copy_from_user(kname, kargs.name, nlen)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    kname[nlen] = '\0'; // Ensure null-termination

    process_t *process = process_create(kname);
    if (!process) {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    for (int i = 0; i < 4; i++) {
        handle_entry_t *src = handle_vec_get(&current_thread->owner_process->handle_table, i);
        if (!src || src->type == HANDLE_FREE)
            continue;
        handle_entry_t *dst = handle_vec_get(&process->handle_table, i);
        *dst = *src;
        if (src->type == HANDLE_ENDPOINT && src->ep)
            src->ep->ref_count++;
    }
    
    process_set_parent(process, current_thread->owner_process);

    // now return a handle 
    int slot = handle_vec_find_free(&current_thread->owner_process->handle_table);
    if (slot < 0) {
        process_destroy(process);
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }
    handle_vec_get(&current_thread->owner_process->handle_table, slot)->type = HANDLE_TASK;
    handle_vec_get(&current_thread->owner_process->handle_table, slot)->task = process;
    handle_vec_get(&current_thread->owner_process->handle_table, slot)->grantable = true;

    (*arch_reg(frame, 0)) = slot;
    (*arch_reg(frame, 1)) = process->pid;
    return;
}

void sys_kickstart(arch_regs_t *frame) {
    kickstart_args_t *args = (kickstart_args_t *)(*arch_reg(frame, 0));
    if (!validate_user_ptr((uintptr_t)args, sizeof(kickstart_args_t))) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    kickstart_args_t kargs;
    if (!copy_from_user(&kargs, args, sizeof(kickstart_args_t))) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    if (kargs.size < sizeof(kickstart_args_t)) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, kargs.task_handle);
    if (!entry) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    if (entry->type != HANDLE_TASK) {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return;
    }

    process_t *target = entry->task;
    if (!target || !target->thread) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    if (target->thread->state != FROZEN) {
        (*arch_reg(frame, 0)) = ERR_BUSY;
        return;
    }

    target->thread->kernel_sp = (uint32_t *)arch_thread_user_init(
        (void *)target->thread->kernel_stack_top,
        kargs.entry, kargs.sp, USER_ELF_BASE,
        kargs.r0_val, kargs.r1_val, &target->thread->trap_frame);
    target->thread->state = READY;
    sched_add(target->thread);
    entry->type = HANDLE_FREE;
    entry->task = NULL;
    entry->grantable = false;
    (*arch_reg(frame, 0)) = 0;
    KDEBUG("Kickstarted process with PID %d", target->pid, kargs.entry);
    return;
}

void sys_pkill(arch_regs_t *frame) {
    uint32_t handle_idx = (*arch_reg(frame, 0));

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    if (entry->type != HANDLE_TASK) {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return;
    }

    process_t *target = entry->task;
    if (!target || !target->thread) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }

    entry->type = HANDLE_FREE;
    entry->task = NULL;
    entry->grantable = false;

    process_destroy(target);

    (*arch_reg(frame, 0)) = 0;
}
