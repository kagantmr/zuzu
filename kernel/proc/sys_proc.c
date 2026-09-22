
#include "sys_proc.h"

#include "kernel/ipc/handle.h"
#include "kernel/proc/process.h"
#include "kernel/sched/sched.h"
#include "kernel/svc/svc.h"
#include "kernel/time/tick.h"

#include <arch/context.h>
#include <arch/mmu.h>
#include <arch/timer.h>

#include <string.h>

#include <zuzu/spawn_args.h>
#include <zuzu/tls.h>
#include <zuzu/user_layout.h>

extern ProcessObj *process_table[MAX_PROCESSES];

#define LOG_FMT(fmt) "(sys_task) " fmt
#include "core/log.h"

#define WAIT_ANY_PID ((Pid) - 1)

static bool wait_write_status(int32_t *status_out, int32_t status)
{
    if (!status_out)
        return true;

    return CopyToUser(status_out, &status, sizeof(status));
}

void SysWait(CpuState *frame)
{
    int32_t req_pid = (int32_t)(*arch_reg(frame, 0));
    int32_t *status_out = (int32_t *)(*arch_reg(frame, 1));
    uint32_t flags = (*arch_reg(frame, 2));
    ProcessObj *child = NULL;

    if (req_pid == -1)
    {
        child = ProcessFindZombieChild(current_thread->owner_process);
        if (child)
        {
            if (!wait_write_status(status_out, child->exit_status))
            {
                arch_reg_set(frame, 0, ERR_BADPTR);
                return;
            }
            arch_reg_set(frame, 0, child->pid);
            ProcessDestroy(child);
            return;
        }

        if (flags & WNOHANG)
        {
            (*arch_reg(frame, 0)) = 0;
            return;
        }

        current_thread->owner_process->waiting_for = WAIT_ANY_PID;
        current_thread->state = BLOCKED;
        Schedule();

        child = ProcessFindZombieChild(current_thread->owner_process);
        if (!child)
        {
            arch_reg_set(frame, 0, ERR_NOENT);
            return;
        }
        if (!wait_write_status(status_out, child->exit_status))
        {
            arch_reg_set(frame, 0, ERR_BADPTR);
            return;
        }
        arch_reg_set(frame, 0, child->pid);
        ProcessDestroy(child);
        return;
    }

    if (req_pid < 0)
    {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    Pid child_pid = req_pid;
    child = ProcessFindChildFromPid(current_thread->owner_process, child_pid);
    if (!child)
    {
        arch_reg_set(frame, 0, ERR_NOENT);
        return;
    }

    // Case A: child already exited
    if (child->thread->state == ZOMBIE)
    {
        if (!wait_write_status(status_out, child->exit_status))
        {
            arch_reg_set(frame, 0, ERR_BADPTR);
            return;
        }
        arch_reg_set(frame, 0, child->pid);
        ProcessDestroy(child);
        return;
    }

    // Case B: child still running, non-blocking
    if (flags & WNOHANG)
    {
        (*arch_reg(frame, 0)) = 0;
        return;
    }

    // Case C: block until child exits
    current_thread->owner_process->waiting_for = child_pid;
    current_thread->state = BLOCKED;
    Schedule();

    // re-fetch after wakeup, pointer may be stale
    child = ProcessFindChildFromPid(current_thread->owner_process, child_pid);
    if (!child)
    {
        arch_reg_set(frame, 0, ERR_NOENT);
        return;
    }
    if (!wait_write_status(status_out, child->exit_status))
    {
        arch_reg_set(frame, 0, ERR_BADPTR);
        return;
    }
    arch_reg_set(frame, 0, child->pid);
    ProcessDestroy(child);
}

/* spawn syscall removed: use pspawn/kickstart with sysd */

void SysPSpawn(CpuState *frame)
{
    CreateSpawnArgs *args = (CreateSpawnArgs *)(*arch_reg(frame, 0));
    if (!validate_user_ptr((uintptr_t)args, sizeof(CreateSpawnArgs)))
    {
        arch_reg_set(frame, 0, ERR_BADPTR);
        return;
    }

    CreateSpawnArgs kargs;
    if (!CopyFromUser(&kargs, args, sizeof(CreateSpawnArgs)))
    {
        arch_reg_set(frame, 0, ERR_BADPTR);
        return;
    }

    if (kargs.size < sizeof(CreateSpawnArgs))
    {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    if (!validate_user_ptr((uintptr_t)kargs.name, 1))
    {
        arch_reg_set(frame, 0, ERR_BADPTR);
        return;
    }

    char kname[64];
    size_t nlen = kargs.name_len;
    if (nlen > sizeof(kname) - 1)
        nlen = sizeof(kname) - 1;
    if (nlen > 0 && !CopyFromUser(kname, kargs.name, nlen))
    {
        arch_reg_set(frame, 0, ERR_BADPTR);
        return;
    }

    kname[nlen] = '\0'; // Ensure null-termination

    ProcessObj *process = ProcessCreate(kname);
    if (!process)
    {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }

    HandleTable *caller_ht = &current_thread->owner_process->handle_table;
    for (int i = 0; i < 4; i++)
    {
        HandleEntry *src = HandleTableGet(caller_ht, (uint32_t)i);
        if (!src || src->type == HANDLE_FREE)
            continue;
        HandleEntry *dst = HandleTableGetOrAlloc(&process->handle_table, (uint32_t)i);
        if (!dst)
            continue;
        *dst = *src;
        HandleEntryClaim(&process->handle_table, dst);
        if (src->type == HANDLE_PORT && src->port)
            src->port->ref_count++;
    }

    ProcessSetParent(process, current_thread->owner_process);

    // now return a handle
    int slot = HandleTableFindFree(caller_ht);
    if (slot < 0)
    {
        ProcessDestroy(process);
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }
    HandleEntry *slot_entry = HandleTableGet(caller_ht, (uint32_t)slot);
    if (!slot_entry)
    {
        ProcessDestroy(process);
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }
    slot_entry->type = HANDLE_TASK;
    slot_entry->task = process;
    slot_entry->grantable = true;
    HandleEntryClaim(caller_ht, slot_entry);

    arch_reg_set(frame, 0, slot);
    arch_reg_set(frame, 1, process->pid);
}

void SysKickstart(CpuState *frame)
{

    HandleCntlStartArgs *args = (HandleCntlStartArgs *)(*arch_reg(frame, 0));
    if (!validate_user_ptr((uintptr_t)args, sizeof(HandleCntlStartArgs)))
    {
        arch_reg_set(frame, 0, ERR_BADPTR);
        return;
    }

    HandleCntlStartArgs kargs;
    if (!CopyFromUser(&kargs, args, sizeof(HandleCntlStartArgs)))
    {
        arch_reg_set(frame, 0, ERR_BADPTR);
        return;
    }

    if (kargs.size < sizeof(HandleCntlStartArgs))
    {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    HandleEntry *entry =
        HandleTableGet(&current_thread->owner_process->handle_table, (uint32_t)kargs.task_handle);
    if (!entry)
    {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type != HANDLE_TASK)
    {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }

    ProcessObj *target = entry->task;
    if (!target || !target->thread)
    {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (target->thread->state != FROZEN)
    {
        arch_reg_set(frame, 0, ERR_BUSY);
        return;
    }

    target->thread->kernel_sp = (uint32_t *)arch_thread_user_init(
        (void *)target->thread->kernel_stack_top, kargs.entry, kargs.sp, USER_ELF_BASE,
        kargs.r0_val, kargs.r1_val, &target->thread->trap_frame);
    target->thread->state = READY;
    SchedAdd(target->thread);
    (*arch_reg(frame, 0)) = 0;
    KDEBUG("Kickstarted process with PID %d", target->pid, kargs.entry);
    return;
}

void SysPKill(CpuState *frame)
{
    uint32_t handle_idx = (*arch_reg(frame, 0));

    HandleTable *ht = &current_thread->owner_process->handle_table;
    HandleEntry *entry = HandleTableGet(ht, handle_idx);
    if (!entry)
    {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type != HANDLE_TASK)
    {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }

    ProcessObj *target = entry->task;
    if (!target || !target->thread)
    {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }

    if (target == current_thread->owner_process)
    {
        arch_reg_set(frame, 0, ERR_BADARG); /* use pquit */
        return;
    }
    HandleEntryFree(ht, entry);

    ProcessKill(target, KILLED_TAG | KILL_BY_PARENT);

    (*arch_reg(frame, 0)) = 0;
}
