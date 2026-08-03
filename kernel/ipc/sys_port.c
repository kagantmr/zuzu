#include "sys_port.h"
#include "sys_notif.h"
#include "kernel/syscall/syscall.h"
#include "port.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/process.h"

#define LOG_FMT(fmt) "(sys_port) " fmt
#include "core/log.h"

extern thread_t *current_thread;
extern process_t *process_table[MAX_PROCESSES];
endpoint_t *nametable_endpoint;

static bool can_regrant_received_handle(const process_t *grantee)
{
    // only sysd may receive grantable copies.
    // Everyone else gets a non-grantable copy to prevent unbounded handle propagation.
    return grantee && ((grantee->flags & PROC_FLAG_INIT) != 0);
}

void sys_port_create(arch_regs_t *frame)
{
    if (!current_thread)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    Handle handle = handle_vec_find_free(&current_thread->owner_process->handle_table);
    if (handle == -1)
    {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle);

    endpoint_t *new_endpoint = (endpoint_t *)kalloc_endpoint();
    if (!new_endpoint)
    {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }
    if (current_thread->owner_process->flags & PROC_FLAG_INIT && !nametable_endpoint)
    {

        nametable_endpoint = new_endpoint;
        /* Inject NT handle into processes spawned before nametable existed. */
        for (int j = 0; j < MAX_PROCESSES; j++)
        {
            process_t *p = process_table[j];
            if (p && p != current_thread->owner_process)
            {
                handle_entry_t *p_entry = handle_vec_get(&p->handle_table, 0);
                if (p_entry && p_entry->type == HANDLE_FREE)
                {
                    p_entry->ep = nametable_endpoint;
                    p_entry->grantable = true;
                    p_entry->type = HANDLE_ENDPOINT;
                    nametable_endpoint->ref_count++;
                }
                else if (p_entry && p_entry->type != HANDLE_FREE)
                {
                    KWARN("nametable bootstrap skipped PID %u: handle slot 0 already in use (type=%d ep=%p)",
                          p->pid, p_entry->type, (void *)p_entry->ep);
                }
            }
        }
    }
    // list_init(&new_endpoint->node);
    list_init(&new_endpoint->sender_queue);
    list_init(&new_endpoint->receiver_queue);
    new_endpoint->owner_pid = current_thread->owner_process->pid;
    new_endpoint->ref_count = 1;
    new_endpoint->alive = true;
    entry->ep = new_endpoint;
    entry->grantable = true;
    entry->type = HANDLE_ENDPOINT;

    (*arch_reg(frame, 0)) = handle;
    return;
}

void sys_destroy(arch_regs_t *frame)
{
    if (!current_thread)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    int handle = (int)(*arch_reg(frame, 0));

    // Validate handle
    handle_entry_t *entry = handle_vec_get(&current_thread->owner_process->handle_table, handle);
    if (!entry)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    if (entry->type == HANDLE_FREE)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }
    if (entry->type == HANDLE_REPLY || entry->type == HANDLE_TASK)
    {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return;
    }
    switch (entry->type)
    {
    case HANDLE_ENDPOINT:
    {

        endpoint_t *ep = entry->ep;
        if (!ep)
        {
            (*arch_reg(frame, 0)) = ERR_BADHANDLE;
            return;
        }

        if (!ep->alive)
        {
            entry->ep = NULL;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
            (*arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }

        // Only owner can destroy
        if (ep->owner_pid != current_thread->owner_process->pid)
        {
            (*arch_reg(frame, 0)) = ERR_NOPERM;
            return;
        }

        // Wake all blocked senders with error
        while (!list_empty(&ep->sender_queue))
        {
            list_node_t *n = list_pop_front(&ep->sender_queue);
            thread_t *t = container_of(n, thread_t, node);
            t->ipc_state = IPC_NONE;
            t->blocked_endpoint = NULL;
            (*arch_reg(t->trap_frame, 0)) = ERR_DEAD;
            t->state = READY;
            sched_add(t);
        }

        // Wake all blocked receivers with error
        while (!list_empty(&ep->receiver_queue))
        {
            list_node_t *n = list_pop_front(&ep->receiver_queue);
            thread_wait_slot_t *slot = container_of(n, thread_wait_slot_t, node);
            thread_t *t = slot->owner;
            if (t->waitany_ep_wait_active)
            {
                thread_waitany_clear_waits(t);
                thread_waitany_clear_ep_waits(t);
            }
            else
            {
                t->ipc_state = IPC_NONE;
                t->blocked_endpoint = NULL;
            }
            if (t->trap_frame)
                (*arch_reg(t->trap_frame, 0)) = ERR_DEAD;
            if (t->wake_tick != 0 && t->timeout_node.prev && t->timeout_node.next)
                list_remove(&t->timeout_node);
            t->wake_tick = 0;
            t->wake_reason = WAKE_IPC;
            t->state = READY;
            sched_add(t);
        }

        ep->alive = false;

        entry->ep = NULL;
        entry->grantable = false;
        entry->type = HANDLE_FREE;

        if (ep->ref_count > 0)
            ep->ref_count--;
        if (ep->ref_count == 0)
            kfree_endpoint(ep);

        (*arch_reg(frame, 0)) = 0;
    }
    break;
    case HANDLE_NOTIFICATION: {
        Notification *ntf = entry->ntfn;
        if (!ntf)
        {
            (*arch_reg(frame, 0)) = ERR_BADHANDLE;
            return;
        }

        if (!ntf->alive)
        {
            entry->ntfn = NULL;
            entry->grantable = false;
            entry->type = HANDLE_FREE;
            (*arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }

        // Only owner can destroy
        if (ntf->owner_pid != current_thread->owner_process->pid)
        {
            (*arch_reg(frame, 0)) = ERR_NOPERM;
            return;
        }

        // Wake all blocked waiters (plain ntfn_wait and waitany) with error
        while (!list_empty(&ntf->wait_queue))
        {
            list_node_t *n = list_pop_front(&ntf->wait_queue);
            thread_wait_slot_t *slot = container_of(n, thread_wait_slot_t, node);
            ntfn_wake_waiter(ntf, slot, ERR_DEAD, 0);
        }


        ntf->alive = false;

        entry->ntfn = NULL;
        entry->grantable = false;
        entry->type = HANDLE_FREE;

        if (ntf->ref_count > 0)
            ntf->ref_count--;
        if (ntf->ref_count == 0)
            kfree(ntf);

        (*arch_reg(frame, 0)) = 0;
    }
    break;
    case HANDLE_SHMEM: {
        // Mapped handles must go through detach/memunmap so the region is torn down
        if (entry->mapped_va != 0)
        {
            (*arch_reg(frame, 0)) = ERR_BUSY;
            return;
        }

        // Drop this handle's reference; frees the object when it was the last.
        shmem_drop_ref(entry->shm);
        entry->shm = NULL;
        entry->grantable = false;
        entry->type = HANDLE_FREE;

        (*arch_reg(frame, 0)) = 0;
    }
    break;
    case HANDLE_DEVICE: {
        device_cap_t *dev = entry->dev;
        if (!dev)
        {
            (*arch_reg(frame, 0)) = ERR_BADHANDLE;
            return;
        }

        // Refuse while this handle's mapping is live in our address space
        if (entry->mapped_va != 0)
        {
            (*arch_reg(frame, 0)) = ERR_BUSY;
            return;
        }

        entry->dev = NULL;
        entry->mapped_va = 0;
        entry->grantable = false;
        entry->type = HANDLE_FREE;

        if (dev->ref_count > 0)
            dev->ref_count--;
        if (dev->ref_count == 0)
            kfree_device_cap(dev);

        (*arch_reg(frame, 0)) = 0;
    }
    break;
    case HANDLE_TASK: {
        process_t *task = entry->task;
        if (!task) {
            (*arch_reg(frame, 0)) = ERR_BADHANDLE;
            return;
        }
        // Refuse while the process is still alive; it must exit or be pkill'd first.
        if (task->thread && task->thread->state != ZOMBIE) {
            (*arch_reg(frame, 0)) = ERR_BUSY;
            return;
        }

        entry->task = NULL;
        entry->grantable = false;
        entry->type = HANDLE_FREE;
        // reap: drop the parent's reference / free the process_t
        (*arch_reg(frame, 0)) = 0;
    }
    break;
    default: {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
    }
    }
}

void sys_grant(arch_regs_t *frame)
{
    if (!current_thread)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    Handle handle = (Handle)(*arch_reg(frame, 0));
    Pid pid = (*arch_reg(frame, 1));

    // Validate handle
    handle_entry_t *src = handle_vec_get(&current_thread->owner_process->handle_table, (uint32_t)handle);
    if (!src || src->type == HANDLE_FREE)
    {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }

    if (!src->grantable || current_thread->owner_process->pid == pid)
    {
        (*arch_reg(frame, 0)) = ERR_NOPERM;
        return;
    }

    if (src->type == HANDLE_REPLY)
    {
        (*arch_reg(frame, 0)) = ERR_NOPERM;
        return;
    }

    // Look up target process
    process_t *grantee = process_find_by_pid(pid);
    if (!grantee)
    {
        (*arch_reg(frame, 0)) = ERR_NOENT;
        return;
    }
    if (grantee->thread->state == ZOMBIE)
    {
        (*arch_reg(frame, 0)) = ERR_DEAD;
        return;
    }

    int slot = handle_vec_find_free(&grantee->handle_table);
    if (slot < 0)
    {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    handle_entry_t *dst = handle_vec_get(&grantee->handle_table, (uint32_t)slot);
    if (!dst)
    {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    *dst = *src;

    if (dst->type == HANDLE_ENDPOINT)
    {
        if (!dst->ep || !dst->ep->alive)
        {
            dst->type = HANDLE_FREE;
            dst->grantable = false;
            dst->ep = NULL;
            (*arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }
        dst->ep->ref_count++;
    }
    if (dst->type == HANDLE_DEVICE)
    {
        if (!dst->dev)
        {
            dst->type = HANDLE_FREE;
            dst->grantable = false;
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
        dst->dev->ref_count++;
    }
    if (dst->type == HANDLE_NOTIFICATION)
    {
        if (!dst->ntfn || !dst->ntfn->alive)
        {
            dst->type = HANDLE_FREE;
            dst->grantable = false;
            dst->ntfn = NULL;
            (*arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }
        dst->ntfn->ref_count++;
    }

    if (dst->type == HANDLE_SHMEM)
    {
        dst->mapped_va = 0;         // the grantee has its own (unmapped) handle
        if (dst->shm)
            dst->shm->ref_count++;  // new handle reference to the same object
    }
    dst->grantable = can_regrant_received_handle(grantee);
    (*arch_reg(frame, 0)) = (Handle)slot;
}

void SysStamp(arch_regs_t *frame)
{
    Handle   src_handle = (*arch_reg(frame, 0));
    uint32_t value      = (*arch_reg(frame, 1));

    // 1. value != 0  (0 is the reserved unmarked sentinel)
    if (value == MARKER_NONE) {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    // 2. resolve the source handle
    handle_entry_t *src = handle_vec_get(&current_thread->owner_process->handle_table, src_handle);
    if (!src) {
        (*arch_reg(frame, 0)) = ERR_BADHANDLE;
        return;
    }

    // 3. must be an endpoint cap
    if (src->type != HANDLE_ENDPOINT) {
        (*arch_reg(frame, 0)) = ERR_BADTYPE;
        return;
    }
    if (!src->ep || !src->ep->alive) {
        (*arch_reg(frame, 0)) = ERR_DEAD;   // or ERR_BADHANDLE for !ep
        return;
    }

    // 4. IMMUTABILITY: can only stamp an UNMARKERD cap
    if (src->marker != MARKER_NONE) {
        (*arch_reg(frame, 0)) = ERR_DUPLICATE;   // already markerd, won't re-stamp
        return;
    }

    // 5. allocate a new slot in the CALLER's table
    int slot = handle_vec_find_free(&current_thread->owner_process->handle_table);
    if (slot < 0) {
        (*arch_reg(frame, 0)) = ERR_NOMEM;
        return;
    }

    // 6. new entry: SAME endpoint, marker = value
    handle_entry_t *ne = handle_vec_get(&current_thread->owner_process->handle_table, slot);
    ne->type      = HANDLE_ENDPOINT;
    ne->ep        = src->ep;      // same underlying endpoint object
    ne->marker     = value;        // the stamp
    ne->grantable = src->grantable;   // inherit grantability (see note)
    src->ep->ref_count++;

    // 7. return the new handle; src is UNTOUCHED (non-consuming)
    (*arch_reg(frame, 0)) = slot;
}