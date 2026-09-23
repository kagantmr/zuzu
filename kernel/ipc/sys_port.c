#include "kernel/space/handle.h"
#include "kernel/mm/alloc.h"
#include "kernel/task/task.h"
#include "kernel/space/space.h"
#include "kernel/sched/sched.h"
#include "kernel/svc/svc.h"
#include "port.h"
#include "zuzu/err.h"

#define LOG_FMT(fmt) "(sys_port) " fmt
#include <zuzu/log.h>

void SysDestroy(CpuState *frame)
{
    if (!current_task)
    {
        ArchSetInFrame(frame, 0, ERR_BADARG);
        return;
    }

    int handle = (int)(*ArchGetFromFrame(frame, 0));

    // Validate handle
    HandleTable *ht = &CURRENT_SPACE->handle_table;
    HandleTableEntry *entry = HandleTableGet(ht, handle);
    if (!entry)
    {
        ArchSetInFrame(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type == HANDLE_FREE)
    {
        ArchSetInFrame(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type == HANDLE_REPLY || entry->type == HANDLE_TASK)
    {
        ArchSetInFrame(frame, 0, ERR_BADTYPE);
        return;
    }
    switch (entry->type)
    {
    case HANDLE_PORT:
    {

        PortObject *port = entry->port;
        if (!port)
        {
            ArchSetInFrame(frame, 0, ERR_BADHANDLE);
            return;
        }

        if (!port->alive)
        {
            HandleEntryFree(ht, entry);
            ArchSetInFrame(frame, 0, ERR_DEAD);
            return;
        }

        // Only owner can destroy
        if (port->owner_spid != current_task->owner_process->pid)
        {
            ArchSetInFrame(frame, 0, ERR_NOPERM);
            return;
        }

        // Wake all blocked senders with error
        while (!list_empty(&port->sender_queue))
        {
            ListNode *n = list_pop_front(&port->sender_queue);
            TaskObject *t = container_of(n, TaskObject, node);
            t->ipc_state = IPC_NONE;
            t->blocked_port = NULL;
            ArchSetInFrame(t->trap_frame, 0, ERR_DEAD);
            t->state = READY;
            SchedAdd(t);
        }

        // Wake all blocked receivers with error
        while (!list_empty(&port->receiver_queue))
        {
            ListNode *n = list_pop_front(&port->receiver_queue);
            WaitSlot *slot = container_of(n, WaitSlot, node);
            TaskObject *t = slot->owner;
            t->ipc_state = IPC_NONE;
            t->blocked_port = NULL;
            if (t->trap_frame)
                ArchSetInFrame(t->trap_frame, 0, ERR_DEAD);
            SchedRemoveSleepQueue(t);
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
            PortObjFree(port);

        (*ArchGetFromFrame(frame, 0)) = 0;
    }
    break;
    case HANDLE_NTFN:
    {
        EventObject *ntf = entry->ntfn;
        if (!ntf)
        {
            ArchSetInFrame(frame, 0, ERR_BADHANDLE);
            return;
        }

        if (!ntf->alive)
        {
            HandleEntryFree(ht, entry);
            ArchSetInFrame(frame, 0, ERR_DEAD);
            return;
        }

        // Only owner can destroy
        if (ntf->owner_pid != current_task->owner_process->pid)
        {
            ArchSetInFrame(frame, 0, ERR_NOPERM);
            return;
        }

        // Wake all blocked ntfn_wait waiters with error
        while (!list_empty(&ntf->wait_queue))
        {
            ListNode *n = list_pop_front(&ntf->wait_queue);
            WaitSlot *slot = container_of(n, WaitSlot, node);
            EventWakeWaiter(ntf, slot, ERR_DEAD);
        }

        ntf->alive = false;

        HandleEntryFree(ht, entry);

        if (ntf->ref_count > 0)
            ntf->ref_count--;
        if (ntf->ref_count == 0)
            EventObjFree(ntf);

        (*ArchGetFromFrame(frame, 0)) = 0;
    }
    break;
    case HANDLE_SHM:
    {
        // Mapped handles must go through detach/memunmap so the region is torn down
        if (entry->mapped_va != 0)
        {
            ArchSetInFrame(frame, 0, ERR_BUSY);
            return;
        }

        // Drop this handle's reference; frees the object when it was the last.
        ShmemDropReference(entry->shm);
        HandleEntryFree(ht, entry);

        (*ArchGetFromFrame(frame, 0)) = 0;
    }
    break;
    case HANDLE_DEVICE:
    {
        DeviceCap *dev = entry->dev;
        if (!dev)
        {
            ArchSetInFrame(frame, 0, ERR_BADHANDLE);
            return;
        }

        // Refuse while this handle's mapping is live in our address space
        if (entry->mapped_va != 0)
        {
            ArchSetInFrame(frame, 0, ERR_BUSY);
            return;
        }

        HandleEntryFree(ht, entry);

        if (dev->ref_count > 0)
            dev->ref_count--;
        if (dev->ref_count == 0)
            KFreeDevCap(dev);

        (*ArchGetFromFrame(frame, 0)) = 0;
    }
    break;
    case HANDLE_TASK:
    {
        SpaceObject *task = entry->task;
        if (!task)
        {
            ArchSetInFrame(frame, 0, ERR_BADHANDLE);
            return;
        }
        // Refuse while the process is still alive; it must exit or be pkill'd first.
        if (task->thread && task->thread->state != ZOMBIE)
        {
            ArchSetInFrame(frame, 0, ERR_BUSY);
            return;
        }

        HandleEntryFree(ht, entry);
        // reap: drop the parent's reference / free the process_t
        (*ArchGetFromFrame(frame, 0)) = 0;
    }
    break;
    default:
    {
        ArchSetInFrame(frame, 0, ERR_BADTYPE);
    }
    }
}

void SysGrant(CpuState *frame)
{
    if (!current_task)
    {
        ArchSetInFrame(frame, 0, ERR_BADARG);
        return;
    }

    Handle handle = (Handle)(*ArchGetFromFrame(frame, 0));
    Spid pid = (Spid)(*ArchGetFromFrame(frame, 1));
    uint32_t flags = (*ArchGetFromFrame(frame, 2));

    // Validate handle
    HandleTableEntry *src =
        HandleTableGet(&current_task->owner_process->handle_table, (uint32_t)handle);
    if (!src || src->type == HANDLE_FREE)
    {
        ArchSetInFrame(frame, 0, ERR_BADHANDLE);
        return;
    }

    if (!src->grantable || current_task->owner_process->pid == pid)
    {
        ArchSetInFrame(frame, 0, ERR_NOPERM);
        return;
    }

    if (src->type == HANDLE_REPLY)
    {
        ArchSetInFrame(frame, 0, ERR_NOPERM);
        return;
    }

    // Look up target process
    SpaceObject *grantee = ProcessFindByPid(pid);
    if (!grantee)
    {
        ArchSetInFrame(frame, 0, ERR_NOENT);
        return;
    }
    if (grantee->thread->state == ZOMBIE)
    {
        ArchSetInFrame(frame, 0, ERR_DEAD);
        return;
    }

    HandleTable *grantee_ht = &grantee->handle_table;
    int slot = HandleTableFindFree(grantee_ht);
    if (slot < 0)
    {
        ArchSetInFrame(frame, 0, ERR_NOMEM);
        return;
    }

    HandleTableEntry *dst = HandleTableGet(grantee_ht, (uint32_t)slot);
    if (!dst)
    {
        ArchSetInFrame(frame, 0, ERR_NOMEM);
        return;
    }

    *dst = *src;

    if (dst->type == HANDLE_PORT)
    {
        if (!dst->port || !dst->port->alive)
        {
            HandleEntryFree(grantee_ht, dst);
            ArchSetInFrame(frame, 0, ERR_DEAD);
            return;
        }
        dst->port->ref_count++;
    }
    if (dst->type == HANDLE_DEVICE)
    {
        if (!dst->dev)
        {
            HandleEntryFree(grantee_ht, dst);
            ArchSetInFrame(frame, 0, ERR_BADARG);
            return;
        }
        dst->dev->ref_count++;
    }
    if (dst->type == HANDLE_NTFN)
    {
        if (!dst->ntfn || !dst->ntfn->alive)
        {
            HandleEntryFree(grantee_ht, dst);
            ArchSetInFrame(frame, 0, ERR_DEAD);
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
    ArchSetInFrame(frame, 0, (Handle)slot);
}

void SysStamp(CpuState *frame)
{
    Handle src_handle = (Handle)(*ArchGetFromFrame(frame, 0));
    uint32_t value = (*ArchGetFromFrame(frame, 1));

    // 1. value != 0  (0 is the reserved unmarked sentinel)
    if (value == MARKER_NONE)
    {
        ArchSetInFrame(frame, 0, ERR_BADARG);
        return;
    }

    // 2. resolve the source handle
    HandleTable *ht = &current_task->owner_process->handle_table;
    HandleTableEntry *src = HandleTableGet(ht, (uint32_t)src_handle);
    if (!src)
    {
        ArchSetInFrame(frame, 0, ERR_BADHANDLE);
        return;
    }

    // 3. must be an endpoint cap
    if (src->type != HANDLE_PORT)
    {
        ArchSetInFrame(frame, 0, ERR_BADTYPE);
        return;
    }
    if (!src->port || !src->port->alive)
    {
        ArchSetInFrame(frame, 0, ERR_DEAD); // or ERR_BADHANDLE for !port
        return;
    }

    // 4. IMMUTABILITY: can only stamp an UNMARKERD cap
    if (src->marker != MARKER_NONE)
    {
        ArchSetInFrame(frame, 0, ERR_DUPLICATE); // already markerd, won't re-stamp
        return;
    }

    // 5. allocate a new slot in the CALLER's table. FindFree may grow (and
    // thus realloc) the table, invalidating src -- capture what we need first.
    PortObject *src_port = src->port;
    bool src_grantable = src->grantable;

    int slot = HandleTableFindFree(ht);
    if (slot < 0)
    {
        ArchSetInFrame(frame, 0, ERR_NOMEM);
        return;
    }

    // 6. new entry: SAME endpoint, marker = value
    HandleTableEntry *ne = HandleTableGet(ht, (uint32_t)slot);
    if (!ne)
    {
        ArchSetInFrame(frame, 0, ERR_NOMEM);
        return;
    }
    ne->type = HANDLE_PORT;
    ne->port = src_port;           // same underlying port object
    ne->marker = value;            // the stamp
    ne->grantable = src_grantable; // inherit grantability (see note)
    HandleEntryClaim(ht, ne);
    src_port->ref_count++;

    // 7. return the new handle; src is UNTOUCHED (non-consuming)
    ArchSetInFrame(frame, 0, slot);
}

#define LABEL_SELF (-2)

void SysSetLabel(CpuState *frame)
{
    Handle src_handle = (Handle)(*ArchGetFromFrame(frame, 0));
    Label value = (*arch_reg(frame, 1));

    if (!(current_task->owner_process->flags & PROC_FLAG_INIT))
    {
        ArchSetInFrame(frame, 0, ERR_NOPERM);
        return;
    }

    // 1. value != 0  (0 is the reserved unmarked sentinel)
    if (value == LABEL_NONE)
    {
        ArchSetInFrame(frame, 0, ERR_BADARG);
        return;
    }

    SpaceObject *target;

    if (src_handle == LABEL_SELF)
    {
        target = current_task->owner_process;
    }
    else
    {

        HandleTableEntry *src = HandleTableGet(&current_task->owner_process->handle_table, (uint32_t)src_handle);
        if (!src)
        {
            ArchSetInFrame(frame, 0, ERR_BADHANDLE);
            return;
        }

        if (src->type != HANDLE_TASK)
        {
            ArchSetInFrame(frame, 0, ERR_BADTYPE);
            return;
        }

        target = src->task;

        if (!target || !target->thread)
        {
            ArchSetInFrame(frame, 0, ERR_BADHANDLE);
            return;
        }

        if (target->thread->state != FROZEN)
        {
            ArchSetInFrame(frame, 0, ERR_BUSY);
            return;
        }
    }
    if (target->label != LABEL_NONE)
    {
        ArchSetInFrame(frame, 0, ERR_DUPLICATE);
        return;
    }

    target->label = value;

    (*ArchGetFromFrame(frame, 0)) = ZUZU_OK;
}
