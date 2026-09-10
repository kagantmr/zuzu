#include "sys_port.h"
#include "handle.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/process.h"
#include "kernel/proc/thread.h"
#include "kernel/sched/sched.h"
#include "kernel/syscall/syscall.h"
#include "port.h"
#include "zuzu/err.h"

#define LOG_FMT(fmt) "(sys_port) " fmt
#include <zuzu/log.h>

extern ProcessObj *process_table[MAX_PROCESSES];

static bool CanRegrantHandle(const ProcessObj *grantee)
{
    // only sysd may receive grantable copies.
    // Everyone else gets a non-grantable copy to prevent unbounded handle propagation.
    return grantee && ((grantee->flags & PROC_FLAG_INIT) != 0);
}

void SysPortCreate(CpuState *frame)
{
    if (!current_thread)
    {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    Handle handle = HandleTableFindFree(&current_thread->owner_process->handle_table);
    if (handle == -1)
    {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }

    HandleTable *ht = &current_thread->owner_process->handle_table;
    HandleEntry *entry = HandleTableGet(ht, (uint32_t)handle);

    Port *new_port = (Port *)KAllocPortObj();
    if (!new_port)
    {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }
    // list_init(&new_port->node);
    list_init(&new_port->sender_queue);
    list_init(&new_port->receiver_queue);
    new_port->owner_pid = current_thread->owner_process->pid;
    new_port->ref_count = 1;
    new_port->alive = true;
    entry->port = new_port;
    entry->grantable = true;
    entry->type = HANDLE_PORT;
    HandleEntryClaim(ht, entry);

    arch_reg_set(frame, 0, handle);
}

void SysDestroy(CpuState *frame)
{
    if (!current_thread)
    {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    int handle = (int)(*arch_reg(frame, 0));

    // Validate handle
    HandleTable *ht = &current_thread->owner_process->handle_table;
    HandleEntry *entry = HandleTableGet(ht, (uint32_t)handle);
    if (!entry)
    {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type == HANDLE_FREE)
    {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type == HANDLE_REPLY || entry->type == HANDLE_TASK)
    {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }
    switch (entry->type)
    {
    case HANDLE_PORT:
    {

        Port *port = entry->port;
        if (!port)
        {
            arch_reg_set(frame, 0, ERR_BADHANDLE);
            return;
        }

        if (!port->alive)
        {
            HandleEntryFree(ht, entry);
            arch_reg_set(frame, 0, ERR_DEAD);
            return;
        }

        // Only owner can destroy
        if (port->owner_pid != current_thread->owner_process->pid)
        {
            arch_reg_set(frame, 0, ERR_NOPERM);
            return;
        }

        // Wake all blocked senders with error
        while (!list_empty(&port->sender_queue))
        {
            ListNode *n = list_pop_front(&port->sender_queue);
            Thread *t = container_of(n, Thread, node);
            t->ipc_state = IPC_NONE;
            t->blocked_port = NULL;
            arch_reg_set(t->trap_frame, 0, ERR_DEAD);
            t->state = READY;
            SchedAdd(t);
        }

        // Wake all blocked receivers with error
        while (!list_empty(&port->receiver_queue))
        {
            ListNode *n = list_pop_front(&port->receiver_queue);
            ThreadWaitSlot *slot = container_of(n, ThreadWaitSlot, node);
            Thread *t = slot->owner;
            if (t->waitany_port_wait_active)
            {
                ThreadWaitanyClearWaits(t);
                ThreadWaitanyClearPortWaits(t);
            }
            else
            {
                t->ipc_state = IPC_NONE;
                t->blocked_port = NULL;
            }
            if (t->trap_frame)
                arch_reg_set(t->trap_frame, 0, ERR_DEAD);
            if (t->wake_deadline != 0 && t->timeout_node.prev && t->timeout_node.next)
                list_remove(&t->timeout_node);
            t->wake_deadline = 0;
            t->wake_reason = WAKE_IPC;
            t->state = READY;
            SchedAdd(t);
        }

        port->alive = false;

        HandleEntryFree(ht, entry);

        if (port->ref_count > 0)
            port->ref_count--;
        if (port->ref_count == 0)
            KFreePortObj(port);

        (*arch_reg(frame, 0)) = 0;
    }
    break;
    case HANDLE_NTFN:
    {
        NtfnObj *ntf = entry->ntfn;
        if (!ntf)
        {
            arch_reg_set(frame, 0, ERR_BADHANDLE);
            return;
        }

        if (!ntf->alive)
        {
            HandleEntryFree(ht, entry);
            arch_reg_set(frame, 0, ERR_DEAD);
            return;
        }

        // Only owner can destroy
        if (ntf->owner_pid != current_thread->owner_process->pid)
        {
            arch_reg_set(frame, 0, ERR_NOPERM);
            return;
        }

        // Wake all blocked waiters (plain ntfn_wait and waitany) with error
        while (!list_empty(&ntf->wait_queue))
        {
            ListNode *n = list_pop_front(&ntf->wait_queue);
            ThreadWaitSlot *slot = container_of(n, ThreadWaitSlot, node);
            NtfnWakeWaiter(ntf, slot, ERR_DEAD, 0);
        }

        ntf->alive = false;

        HandleEntryFree(ht, entry);

        if (ntf->ref_count > 0)
            ntf->ref_count--;
        if (ntf->ref_count == 0)
            KFreeNtfn(ntf);

        (*arch_reg(frame, 0)) = 0;
    }
    break;
    case HANDLE_SHM:
    {
        // Mapped handles must go through detach/memunmap so the region is torn down
        if (entry->mapped_va != 0)
        {
            arch_reg_set(frame, 0, ERR_BUSY);
            return;
        }

        // Drop this handle's reference; frees the object when it was the last.
        ShmemDropReference(entry->shm);
        HandleEntryFree(ht, entry);

        (*arch_reg(frame, 0)) = 0;
    }
    break;
    case HANDLE_DEVICE:
    {
        DeviceCap *dev = entry->dev;
        if (!dev)
        {
            arch_reg_set(frame, 0, ERR_BADHANDLE);
            return;
        }

        // Refuse while this handle's mapping is live in our address space
        if (entry->mapped_va != 0)
        {
            arch_reg_set(frame, 0, ERR_BUSY);
            return;
        }

        HandleEntryFree(ht, entry);

        if (dev->ref_count > 0)
            dev->ref_count--;
        if (dev->ref_count == 0)
            KFreeDevCap(dev);

        (*arch_reg(frame, 0)) = 0;
    }
    break;
    case HANDLE_TASK:
    {
        ProcessObj *task = entry->task;
        if (!task)
        {
            arch_reg_set(frame, 0, ERR_BADHANDLE);
            return;
        }
        // Refuse while the process is still alive; it must exit or be pkill'd first.
        if (task->thread && task->thread->state != ZOMBIE)
        {
            arch_reg_set(frame, 0, ERR_BUSY);
            return;
        }

        HandleEntryFree(ht, entry);
        // reap: drop the parent's reference / free the process_t
        (*arch_reg(frame, 0)) = 0;
    }
    break;
    default:
    {
        arch_reg_set(frame, 0, ERR_BADTYPE);
    }
    }
}

void SysGrant(CpuState *frame)
{
    if (!current_thread)
    {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    Handle handle = (Handle)(*arch_reg(frame, 0));
    Pid pid = (Pid)(*arch_reg(frame, 1));
    uint32_t flags = (*arch_reg(frame, 2));

    // Validate handle
    HandleEntry *src =
        HandleTableGet(&current_thread->owner_process->handle_table, (uint32_t)handle);
    if (!src || src->type == HANDLE_FREE)
    {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }

    if (!src->grantable || current_thread->owner_process->pid == pid)
    {
        arch_reg_set(frame, 0, ERR_NOPERM);
        return;
    }

    if (src->type == HANDLE_REPLY)
    {
        arch_reg_set(frame, 0, ERR_NOPERM);
        return;
    }

    // Look up target process
    ProcessObj *grantee = ProcessFindByPid(pid);
    if (!grantee)
    {
        arch_reg_set(frame, 0, ERR_NOENT);
        return;
    }
    if (grantee->thread->state == ZOMBIE)
    {
        arch_reg_set(frame, 0, ERR_DEAD);
        return;
    }

    HandleTable *grantee_ht = &grantee->handle_table;
    int slot = HandleTableFindFree(grantee_ht);
    if (slot < 0)
    {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }

    HandleEntry *dst = HandleTableGet(grantee_ht, (uint32_t)slot);
    if (!dst)
    {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }

    *dst = *src;

    if (dst->type == HANDLE_PORT)
    {
        if (!dst->port || !dst->port->alive)
        {
            HandleEntryFree(grantee_ht, dst);
            arch_reg_set(frame, 0, ERR_DEAD);
            return;
        }
        dst->port->ref_count++;
    }
    if (dst->type == HANDLE_DEVICE)
    {
        if (!dst->dev)
        {
            HandleEntryFree(grantee_ht, dst);
            arch_reg_set(frame, 0, ERR_BADARG);
            return;
        }
        dst->dev->ref_count++;
    }
    if (dst->type == HANDLE_NTFN)
    {
        if (!dst->ntfn || !dst->ntfn->alive)
        {
            HandleEntryFree(grantee_ht, dst);
            arch_reg_set(frame, 0, ERR_DEAD);
            return;
        }
        dst->ntfn->ref_count++;
    }

    if (dst->type == HANDLE_SHM)
    {
        dst->mapped_va = 0; // the grantee has its own (unmapped) handle
        if (dst->shm)
            dst->shm->ref_count++; // new handle reference to the same object
    }
    dst->grantable = (flags & GRANT_REGRANTABLE) || CanRegrantHandle(grantee);
    HandleEntryClaim(grantee_ht, dst);
    arch_reg_set(frame, 0, (Handle)slot);
}

void SysStamp(CpuState *frame)
{
    Handle src_handle = (Handle)(*arch_reg(frame, 0));
    uint32_t value = (*arch_reg(frame, 1));

    // 1. value != 0  (0 is the reserved unmarked sentinel)
    if (value == MARKER_NONE)
    {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    // 2. resolve the source handle
    HandleTable *ht = &current_thread->owner_process->handle_table;
    HandleEntry *src = HandleTableGet(ht, (uint32_t)src_handle);
    if (!src)
    {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }

    // 3. must be an endpoint cap
    if (src->type != HANDLE_PORT)
    {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }
    if (!src->port || !src->port->alive)
    {
        arch_reg_set(frame, 0, ERR_DEAD); // or ERR_BADHANDLE for !port
        return;
    }

    // 4. IMMUTABILITY: can only stamp an UNMARKERD cap
    if (src->marker != MARKER_NONE)
    {
        arch_reg_set(frame, 0, ERR_DUPLICATE); // already markerd, won't re-stamp
        return;
    }

    // 5. allocate a new slot in the CALLER's table. FindFree may grow (and
    // thus realloc) the table, invalidating src -- capture what we need first.
    Port *src_port = src->port;
    bool src_grantable = src->grantable;

    int slot = HandleTableFindFree(ht);
    if (slot < 0)
    {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }

    // 6. new entry: SAME endpoint, marker = value
    HandleEntry *ne = HandleTableGet(ht, (uint32_t)slot);
    if (!ne)
    {
        arch_reg_set(frame, 0, ERR_NOMEM);
        return;
    }
    ne->type = HANDLE_PORT;
    ne->port = src_port;           // same underlying port object
    ne->marker = value;            // the stamp
    ne->grantable = src_grantable; // inherit grantability (see note)
    HandleEntryClaim(ht, ne);
    src_port->ref_count++;

    // 7. return the new handle; src is UNTOUCHED (non-consuming)
    arch_reg_set(frame, 0, slot);
}

#define LABEL_SELF (-2)

void SysSetLabel(CpuState *frame)
{
    Handle src_handle = (Handle)(*arch_reg(frame, 0));
    Label value = (*arch_reg(frame, 1));

    if (!(current_thread->owner_process->flags & PROC_FLAG_INIT))
    {
        arch_reg_set(frame, 0, ERR_NOPERM);
        return;
    }

    // 1. value != 0  (0 is the reserved unmarked sentinel)
    if (value == LABEL_NONE)
    {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }

    ProcessObj *target;

    if (src_handle == LABEL_SELF)
    {
        target = current_thread->owner_process;
    }
    else
    {

        HandleEntry *src = HandleTableGet(&current_thread->owner_process->handle_table, (uint32_t)src_handle);
        if (!src)
        {
            arch_reg_set(frame, 0, ERR_BADHANDLE);
            return;
        }

        if (src->type != HANDLE_TASK)
        {
            arch_reg_set(frame, 0, ERR_BADTYPE);
            return;
        }

        target = src->task;

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
    }
    if (target->label != LABEL_NONE)
    {
        arch_reg_set(frame, 0, ERR_DUPLICATE);
        return;
    }

    target->label = value;

    (*arch_reg(frame, 0)) = ZUZU_OK;
}
