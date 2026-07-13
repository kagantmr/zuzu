#include <arch/context.h>
#include "sys_task.h"
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
#include <zuzu/ipcx.h>
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
    
    // Change state
    (*arch_reg(frame, 0)) = 0;
    current_thread->state = BLOCKED;
    sleep_queue_insert(current_thread);
    // Schedule someone else immediately
    schedule();
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
    const char* name = (const char *)(*arch_reg(frame, 0));
    if (!validate_user_ptr((uintptr_t)name, 1)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    char kname[64];
    if (!copy_from_user(kname, name, sizeof(kname) - 1)) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    kname[sizeof(kname) - 1] = '\0'; // Ensure null-termination

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
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, kargs.task_handle);
    if (!entry || entry->type != HANDLE_TASK) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    process_t *target = entry->task;
    if (!target || !target->thread || target->thread->state != FROZEN) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
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
    if (!entry || entry->type != HANDLE_TASK) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    process_t *target = entry->task;
    if (!target || !target->thread) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    entry->type = HANDLE_FREE;
    entry->task = NULL;
    entry->grantable = false;

    process_destroy(target);

    (*arch_reg(frame, 0)) = 0;
}

void sys_tmake(arch_regs_t *frame) {
    vaddr_t entry  = (*arch_reg(frame, 0));
    vaddr_t usr_sp = (*arch_reg(frame, 1));
    vaddr_t arg    = (*arch_reg(frame, 2));

    if (!validate_user_ptr(entry, 1)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }
    if (!validate_user_ptr(usr_sp, 4)) {
        (*arch_reg(frame, 0)) = ERR_BADPTR;
        return;
    }

    process_t *owner = current_thread->owner_process;
    thread_t *t = thread_create(owner);
    if (!t) {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    // Temporarily share main thread's IPCX buffer until per-thread IPCX is done
    paddr_t ipcx_buf_pa = pmm_alloc_page();
    if (!ipcx_buf_pa) {
        thread_destroy(t);
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }
    t->ipc_buf_pa = ipcx_buf_pa;
    // map it
    vaddr_t mmap_va = owner->mmap_va_next;
    if (!vmm_map_user_page(owner->as, ipcx_buf_pa, mmap_va,
                        VM_PROT_USER | VM_PROT_READ | VM_PROT_WRITE)) {
        pmm_free_page(ipcx_buf_pa);
        thread_destroy(t);
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }
    // bump owner with overflow check
    if (UINTPTR_MAX - owner->mmap_va_next < PAGE_SIZE) {
        pmm_free_page(ipcx_buf_pa);
        thread_destroy(t);
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    } else {
        owner->mmap_va_next += PAGE_SIZE;
    }

    
    uint32_t slot_idx = owner->tcb_next_slot++;
    if (slot_idx >= TCB_MAX_SLOTS) {
        pmm_free_page(ipcx_buf_pa);
        thread_destroy(t);
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }
    tdata_t *slot = (tdata_t *)(PA_TO_VA(owner->tcb_page_pa) + slot_idx * TCB_SLOT_SIZE);
    slot->ipc_buf = (void *)mmap_va;
    slot->tid = t->tid;
    slot->pid = owner->pid;

    t->thread_info_va = owner->tcb_page_va + slot_idx * TCB_SLOT_SIZE;


    // Build the initial kernel stack so the thread enters user mode at `entry`.
    t->kernel_sp = (uint32_t *)arch_thread_user_init(
        (void *)t->kernel_stack_top, (uintptr_t)entry, (uintptr_t)usr_sp,
        USER_ELF_BASE, (uint32_t)arg, 0, &t->trap_frame);
    t->state = READY;
    sched_add(t);

    (*arch_reg(frame, 0)) = (tid_t)t->tid;
}

void sys_tjoin(arch_regs_t *frame) {
    tid_t tid = (*arch_reg(frame, 0));
    thread_t *thread = thread_find_by_tid(tid);
    if (!thread || thread->owner_process != current_thread->owner_process) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    if (thread->state != ZOMBIE) {
        current_thread->owner_process->waiting_for_tid = tid;
        current_thread->state = BLOCKED;
        schedule();

        /* `process_wake_joiners` delivered the exit status into our
         * trap frame before making us READY; do not access `thread`
         * here since it may have been unregistered/freed by the
         * reaper. The return value is already placed in `(*arch_reg(frame, 0))`.
         */
        return;
    }

    /* Thread already a ZOMBIE: read the exit status (no destroy).
     * Ownership of destruction belongs to the thread that performed
     * the quit (tquit) and the scheduler reaper. */
    (*arch_reg(frame, 0)) = thread->exit_status;
}


void sys_tquit(arch_regs_t *frame) {
    int exit_status = (int)(*arch_reg(frame, 0));
    thread_t *t = current_thread;
    process_t *owner = t->owner_process;

    t->exit_status = exit_status;
    process_wake_joiners(t->tid, exit_status);

    if (owner->threads.node.next == &t->process_node &&
        t->process_node.next == &owner->threads.node) {
        // last thread, kill the process
        process_kill(owner, exit_status);
    } else {
        thread_kill(t);
        // remove from process thread list NOW so process_destroy won't see it
        if (t->process_node.prev && t->process_node.next)
            list_remove(&t->process_node);
        sched_defer_destroy_thread(t);
    }

    schedule();
    __builtin_unreachable();
}