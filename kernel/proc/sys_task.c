#include "sys_task.h"
#include "kernel/syscall/syscall.h"
#include "kernel/mm/alloc.h"
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
    current_process->wake_reason = WAKE_NONE;
    
    // Change state
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
    uintptr_t args_ptr = frame->r[0];
    if (!validate_user_ptr(args_ptr, sizeof(spawn_args_t))) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    spawn_args_t kargs;
    if (!copy_from_user(&kargs, (const void *)args_ptr, sizeof(spawn_args_t))) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    // validate all pointers from the struct
    if (!kargs.elf_data || !kargs.name || kargs.elf_size == 0 || kargs.name_len == 0) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    const void *elf_data = kargs.elf_data;
    size_t elf_size = kargs.elf_size;
    const char *name = kargs.name;
    size_t name_len =  kargs.name_len;
    const char *argbuf = kargs.argbuf;
    size_t argbuf_len = kargs.argbuf_len;
    uint32_t argc = kargs.argc;

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

    // Allocate and copy argument buffer if provided
    void *kargbuf = NULL;
    if (argbuf && argbuf_len > 0) {
        kargbuf = kmalloc(argbuf_len);
        if (!kargbuf) {
            kfree(elf_copy);
            frame->r[0] = ERR_NOMEM;
            return;
        }
        if (!copy_from_user(kargbuf, argbuf, argbuf_len)) {
            kfree(elf_copy);
            kfree(kargbuf);
            frame->r[0] = ERR_BADARG;
            return;
        }
    }

    process_t *process = process_create_from_elf(elf_copy, elf_size, kname, kargbuf, argbuf_len, argc);
    kfree(elf_copy);
    kfree(kargbuf);
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
