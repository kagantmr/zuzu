#include "sys_task.h"
#include "kernel/syscall/syscall.h"
#include "kernel/mm/alloc.h"
#include "kernel/mm/vmm.h"
#include "kstack.h"
#include "arch/arm/mmu/mmu.h"
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

void pquit(exception_frame_t *frame) {
    int exit_status = (int)frame->r[0];
    KDEBUG("Process %d exited with status code %d", 
           current_thread->owner_process ? current_thread->owner_process->pid : 0, 
           exit_status);
    
    process_kill(current_thread->owner_process, exit_status);
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
    current_thread->wake_tick = get_ticks() + ticks;
    current_thread->wake_reason = WAKE_NONE;
    
    // Change state
    frame->r[0] = 0;
    current_thread->state = BLOCKED;
    sleep_queue_insert(current_thread);
    // Schedule someone else immediately
    schedule();
}

void get_pid(exception_frame_t *frame) {
    frame->r[0] = current_thread->owner_process->pid;
}

void wait(exception_frame_t *frame) {
    int32_t req_pid = (int32_t)frame->r[0];
    int32_t *status_out = (int32_t *)frame->r[1];
    uint32_t flags = frame->r[2];
    process_t *child = NULL;

    if (req_pid == -1) {
        child = process_find_zombie_child(current_thread->owner_process);
        if (child) {
            if (!wait_write_status(status_out, child->exit_status)) {
                frame->r[0] = ERR_BADPTR;
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

        current_thread->owner_process->waiting_for = WAIT_ANY_PID;
        current_thread->state = BLOCKED;
        schedule();

        child = process_find_zombie_child(current_thread->owner_process);
        if (!child) {
            frame->r[0] = ERR_NOENT;
            return;
        }
        if (!wait_write_status(status_out, child->exit_status)) {
            frame->r[0] = ERR_BADPTR;
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
    child = process_find_child_by_pid(current_thread->owner_process, child_pid);
    if (!child) {
        frame->r[0] = ERR_NOENT;
        return; 
    }

    // Case A: child already exited
    if (child->thread->state == ZOMBIE) {
        if (!wait_write_status(status_out, child->exit_status)) {
            frame->r[0] = ERR_BADPTR;
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
    current_thread->owner_process->waiting_for = child_pid;
    current_thread->state = BLOCKED;
    schedule();

    // re-fetch after wakeup, pointer may be stale
    child = process_find_child_by_pid(current_thread->owner_process, child_pid);
    if (!child) {
        frame->r[0] = ERR_NOENT;
        return;
    }
    if (!wait_write_status(status_out, child->exit_status)) {
        frame->r[0] = ERR_BADPTR;
        return;
    }
    frame->r[0] = child->pid;
    process_destroy(child);
}

/* spawn syscall removed: use pspawn/kickstart with sysd */

void pspawn(exception_frame_t *frame) {
    const char* name = (const char *)frame->r[0];
    if (!validate_user_ptr((uintptr_t)name, 1)) {
        frame->r[0] = ERR_BADPTR;
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
        frame->r[0] = ERR_NOMEM;
        return;
    }
    handle_vec_get(&current_thread->owner_process->handle_table, slot)->type = HANDLE_TASK;
    handle_vec_get(&current_thread->owner_process->handle_table, slot)->task = process;
    handle_vec_get(&current_thread->owner_process->handle_table, slot)->grantable = true;

    frame->r[0] = slot;
    frame->r[1] = process->pid;
    return;
}

void kickstart(exception_frame_t *frame) {
    kickstart_args_t *args = (kickstart_args_t *)frame->r[0];
    if (!validate_user_ptr((uintptr_t)args, sizeof(kickstart_args_t))) {
        frame->r[0] = ERR_BADPTR;
        return;
    }

    kickstart_args_t kargs;
    if (!copy_from_user(&kargs, args, sizeof(kickstart_args_t))) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, kargs.task_handle);
    if (!entry || entry->type != HANDLE_TASK) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    process_t *target = entry->task;
    if (!target || !target->thread || target->thread->state != FROZEN) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    uintptr_t stack_top = target->thread->kernel_stack_top;

    stack_top -= 17 * sizeof(uintptr_t); // make room for exception frame + r0-r1
    exception_frame_t *target_frame = (exception_frame_t *)stack_top;

    target_frame->r[0] = kargs.r0_val;
    target_frame->r[1] = kargs.r1_val;
    for (int i = 2; i < 13; i++) {
        target_frame->r[i] = 0;
    }
    target_frame->sp_usr = kargs.sp;
    target_frame->lr_usr = USER_ELF_BASE;
    target_frame->return_pc = kargs.entry;
    target_frame->return_cpsr = 0x10; // user mode, interrupts enabled

    stack_top -= sizeof(cpu_context_t);
    cpu_context_t *context = (cpu_context_t *)stack_top;
    memset(context, 0, sizeof(cpu_context_t));
    context->lr = (uint32_t)process_entry_trampoline;
    stack_top -= 132; // make room for VFP state
    memset((void *)stack_top, 0, 132);
    target->thread->kernel_sp = (uint32_t *)stack_top;
    target->thread->state = READY;
    sched_add(target->thread);
    entry->type = HANDLE_FREE;
    entry->task = NULL;
    entry->grantable = false;
    frame->r[0] = 0;
    KDEBUG("Kickstarted process with PID %d", target->pid, kargs.entry);
    return;
}

void kill(exception_frame_t *frame) {
    uint32_t handle_idx = frame->r[0];

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle_idx);
    if (!entry || entry->type != HANDLE_TASK) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    process_t *target = entry->task;
    if (!target || !target->thread) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    entry->type = HANDLE_FREE;
    entry->task = NULL;
    entry->grantable = false;

    process_destroy(target);

    frame->r[0] = 0;
}

void tmake(exception_frame_t *frame) {
    vaddr_t entry  = frame->r[0];
    vaddr_t usr_sp = frame->r[1];
    vaddr_t arg    = frame->r[2];

    if (!validate_user_ptr(entry, 1)) {
        frame->r[0] = ERR_BADPTR;
        return;
    }
    if (!validate_user_ptr(usr_sp, 4)) {
        frame->r[0] = ERR_BADPTR;
        return;
    }

    process_t *owner = current_thread->owner_process;
    thread_t *t = thread_create(owner);
    if (!t) {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    // Temporarily share main thread's IPCX buffer until per-thread IPCX is done
    paddr_t ipcx_buf_pa = pmm_alloc_page();
    if (!ipcx_buf_pa) {
        thread_destroy(t);
        frame->r[0] = ERR_NOMEM;
        return;
    }
    t->ipc_buf_pa = ipcx_buf_pa;
    // map it
    vaddr_t mmap_va = owner->mmap_va_next;
    if (!kmap_user_page(owner->as, ipcx_buf_pa, mmap_va,
                        VM_PROT_USER | VM_PROT_READ | VM_PROT_WRITE)) {
        pmm_free_page(ipcx_buf_pa);
        thread_destroy(t);
        frame->r[0] = ERR_NOMEM;
        return;
    }
    // bump owner with overflow check
    if (UINTPTR_MAX - owner->mmap_va_next < PAGE_SIZE) {
        pmm_free_page(ipcx_buf_pa);
        thread_destroy(t);
        frame->r[0] = ERR_NOMEM;
        return;
    } else {
        owner->mmap_va_next += PAGE_SIZE;
    }

    
    uint32_t slot_idx = owner->tcb_next_slot++;
    if (slot_idx >= TCB_MAX_SLOTS) {
        pmm_free_page(ipcx_buf_pa);
        thread_destroy(t);
        frame->r[0] = ERR_NOMEM;
        return;
    }
    tdata_t *slot = (tdata_t *)(PA_TO_VA(owner->tcb_page_pa) + slot_idx * TCB_SLOT_SIZE);
    slot->ipc_buf = (void *)mmap_va;
    slot->tid = t->tid;
    slot->pid = owner->pid;

    t->thread_info_va = owner->tcb_page_va + slot_idx * TCB_SLOT_SIZE;


    // Build the kernel stack exactly like kernel/kickstart() does
    uintptr_t sp = t->kernel_stack_top;

    // Exception frame (17 words)
    sp -= 17 * sizeof(uint32_t);
    exception_frame_t *ef = (exception_frame_t *)sp;
    memset(ef, 0, 17 * sizeof(uint32_t));
    ef->r[0]        = (uint32_t)arg;
    ef->sp_usr      = (uint32_t)usr_sp;
    ef->lr_usr      = USER_ELF_BASE;
    ef->return_pc   = (uint32_t)entry;
    ef->return_cpsr = 0x10; // USR mode, IRQs enabled

    t->trap_frame = ef;

    // cpu_context
    sp -= sizeof(cpu_context_t);
    cpu_context_t *ctx = (cpu_context_t *)sp;
    memset(ctx, 0, sizeof(cpu_context_t));
    ctx->lr = (uint32_t)process_entry_trampoline;

    // VFP area
    sp -= 132;
    memset((void *)sp, 0, 132);

    t->kernel_sp = (uint32_t *)sp;
    t->state     = READY;
    sched_add(t);

    frame->r[0] = (tid_t)t->tid;
}

void tjoin(exception_frame_t *frame) {
    tid_t tid = frame->r[0];
    thread_t *thread = thread_find_by_tid(tid);
    if (!thread || thread->owner_process != current_thread->owner_process) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    if (thread->state != ZOMBIE) {
        current_thread->owner_process->waiting_for_tid = tid;
        current_thread->state = BLOCKED;
        schedule();

        /* `process_wake_joiners` delivered the exit status into our
         * trap frame before making us READY; do not access `thread`
         * here since it may have been unregistered/freed by the
         * reaper. The return value is already placed in `frame->r[0]`.
         */
        return;
    }

    /* Thread already a ZOMBIE: read the exit status (no destroy).
     * Ownership of destruction belongs to the thread that performed
     * the quit (tquit) and the scheduler reaper. */
    frame->r[0] = thread->exit_status;
}


void tquit(exception_frame_t *frame) {
    int exit_status = (int)frame->r[0];
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