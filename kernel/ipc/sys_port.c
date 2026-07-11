#include "sys_port.h"
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

void port_create(arch_regs_t *frame)
{
    if (!current_thread)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    handle_t handle = handle_vec_find_free(&current_thread->owner_process->handle_table);
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
                if (p_entry && p_entry->type == HANDLE_FREE) {
                    p_entry->ep = nametable_endpoint;
                    p_entry->grantable = true;
                    p_entry->type = HANDLE_ENDPOINT;
                    nametable_endpoint->ref_count++;
                } else if (p_entry && p_entry->type != HANDLE_FREE) {
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

void port_destroy(arch_regs_t *frame)
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
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
    if (entry->type != HANDLE_ENDPOINT)
    {
        (*arch_reg(frame, 0)) = ERR_NOPERM;
        return;
    }
    endpoint_t *ep = entry->ep;

    if (!ep)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
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
        if (t->recvany_ep_wait_active) {
            thread_recvany_clear_waits(t);
            thread_recvany_clear_ep_waits(t);
        } else {
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

void port_grant(arch_regs_t *frame)
{
    if (!current_thread)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    int handle = (int)(*arch_reg(frame, 0));
    zpid_t pid = (*arch_reg(frame, 1));

    // Validate handle
    handle_entry_t *src = handle_vec_get(&current_thread->owner_process->handle_table, (uint32_t)handle);
    if (!src || src->type == HANDLE_FREE)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }

    if (!src->grantable)
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
        (*arch_reg(frame, 0)) = ERR_BUSY;
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

    if (dst->type == HANDLE_ENDPOINT) {
        if (!dst->ep || !dst->ep->alive) {
            dst->type = HANDLE_FREE;
            dst->grantable = false;
            dst->ep = NULL;
            (*arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }
        dst->ep->ref_count++;
    }
    if (dst->type == HANDLE_DEVICE) {
        if (!dst->dev) {
            dst->type = HANDLE_FREE;
            dst->grantable = false;
            (*arch_reg(frame, 0)) = ERR_BADARG;
            return;
        }
        dst->dev->ref_count++;
    }
    if (dst->type == HANDLE_NOTIFICATION) {
        if (!dst->ntfn || !dst->ntfn->alive) {
            dst->type = HANDLE_FREE;
            dst->grantable = false;
            dst->ntfn = NULL;
            (*arch_reg(frame, 0)) = ERR_DEAD;
            return;
        }
        dst->ntfn->ref_count++;
    }

    if (dst->type == HANDLE_SHMEM)
        dst->mapped_va = 0;
    dst->grantable = can_regrant_received_handle(grantee);
    (*arch_reg(frame, 0)) = (handle_t)slot;
}
