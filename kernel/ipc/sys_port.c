#include "sys_port.h"
#include "kernel/syscall/syscall.h"
#include "endpoint.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/process.h"

#define LOG_FMT(fmt) "(sys_port) " fmt
#include "core/log.h"

extern process_t *current_process;
extern process_t *process_table[MAX_PROCESSES];
endpoint_t *nametable_endpoint;

static bool can_regrant_received_handle(const process_t *grantee)
{
    // only zuzusysd may receive grantable copies.
    // Everyone else gets a non-grantable copy to prevent unbounded handle propagation.
    return grantee && ((grantee->flags & PROC_FLAG_INIT) != 0);
}

void port_create(exception_frame_t *frame)
{
    if (!current_process)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    int handle = handle_vec_find_free(&current_process->handle_table);
    if (handle == -1)
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle);

    endpoint_t *new_endpoint = kmalloc(sizeof(endpoint_t));
    if (!new_endpoint)
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }
    if (current_process->flags & PROC_FLAG_INIT && !nametable_endpoint)
    {

        // TODO: move this policy into an explicit bootstrap phase instead of port_create().
        nametable_endpoint = new_endpoint;
        /* Inject NT handle into processes spawned before nametable existed. */
        for (int j = 0; j < MAX_PROCESSES; j++)
        {
            process_t *p = process_table[j]; 
            if (p && p != current_process)
            {
                handle_entry_t *p_entry = handle_vec_get(&p->handle_table, 0);
                if (p_entry && p_entry->type == HANDLE_FREE) {
                    p_entry->ep = nametable_endpoint;
                    p_entry->grantable = true;
                    p_entry->type = HANDLE_ENDPOINT;
                } else if (p_entry && p_entry->type != HANDLE_FREE) {
                    KWARN("nametable bootstrap skipped PID %u: handle slot 0 already in use", p->pid);
                }
            }
        }
    }
    // list_init(&new_endpoint->node);
    list_init(&new_endpoint->sender_queue);
    list_init(&new_endpoint->receiver_queue);
    new_endpoint->owner_pid = current_process->pid;
    new_endpoint->bound_irq = -1;
    entry->ep = new_endpoint;
    entry->grantable = true;
    entry->type = HANDLE_ENDPOINT;

    frame->r[0] = handle;
    return;
}

void port_destroy(exception_frame_t *frame)
{
    if (!current_process)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    int handle = (int)frame->r[0];

    // Validate handle
    handle_entry_t *entry = handle_vec_get(&current_process->handle_table, handle);
    if (!entry)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }
    if (entry->type != HANDLE_ENDPOINT)
    {
        frame->r[0] = ERR_NOPERM;
        return;
    }
    endpoint_t *ep = entry->ep;

    if (!ep)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    // Only owner can destroy
    if (ep->owner_pid != current_process->pid)
    {
        frame->r[0] = ERR_NOPERM;
        return;
    }

    // Wake all blocked senders with error
    while (!list_empty(&ep->sender_queue))
    {
        list_node_t *n = list_pop_front(&ep->sender_queue);
        process_t *proc = container_of(n, process_t, node);
        proc->ipc_state = IPC_NONE;
        proc->blocked_endpoint = NULL;
        proc->trap_frame->r[0] = ERR_DEAD;
        proc->process_state = PROCESS_READY;
        sched_add(proc);
    }

    // Wake all blocked receivers with error
    while (!list_empty(&ep->receiver_queue))
    {
        list_node_t *n = list_pop_front(&ep->receiver_queue);
        process_t *proc = container_of(n, process_t, node);
        proc->ipc_state = IPC_NONE;
        proc->blocked_endpoint = NULL;
        proc->trap_frame->r[0] = ERR_DEAD;
        proc->process_state = PROCESS_READY;
        sched_add(proc);
    }

    // Clean up
    kfree(ep);
    entry->ep = NULL;
    entry->grantable = false;
    entry->type = HANDLE_FREE;
    frame->r[0] = 0;
}

void port_grant(exception_frame_t *frame)
{
    if (!current_process)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    int handle = (int)frame->r[0];
    uint32_t pid = frame->r[1];

    // Validate handle
    handle_entry_t *src = handle_vec_get(&current_process->handle_table, (uint32_t)handle);
    if (!src || src->type == HANDLE_FREE)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    if (!src->grantable)
    {
        frame->r[0] = ERR_NOPERM;
        return;
    }

    if (src->type == HANDLE_REPLY)
    {
        frame->r[0] = ERR_NOPERM;
        return;
    }

    // Look up target process
    process_t *grantee = process_find_by_pid(pid);
    if (!grantee)
    {
        frame->r[0] = ERR_NOENT;
        return;
    }
    if (grantee->process_state == PROCESS_ZOMBIE)
    {
        frame->r[0] = ERR_BUSY;
        return;
    }

    int slot = handle_vec_find_free(&grantee->handle_table);
    if (slot < 0)
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    handle_entry_t *dst = handle_vec_get(&grantee->handle_table, (uint32_t)slot);
    if (!dst)
    {
        frame->r[0] = ERR_NOMEM;
        return;
    }

    *dst = *src;
    dst->grantable = can_regrant_received_handle(grantee);
    if (dst->type == HANDLE_SHMEM && dst->shm)
    {
        dst->shm->ref_count++;
    }

    frame->r[0] = (uint32_t)slot;
}
