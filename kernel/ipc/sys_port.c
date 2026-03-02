#include "sys_port.h"
#include "kernel/syscall/syscall.h"
#include "endpoint.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"

extern process_t *current_process;

void sys_port_create(exception_frame_t *frame)
{
    if (!current_process)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }
    for (int i = 0; i < MAX_HANDLE_TABLE; i++)
    {
        if (!(current_process->handle_table[i]))
        {
            endpoint_t *new_endpoint = kmalloc(sizeof(endpoint_t));
            if (!new_endpoint)
            {
                frame->r[0] = ERR_NOMEM;
                return;
            }
            // list_init(&new_endpoint->node);
            list_init(&new_endpoint->sender_queue);
            list_init(&new_endpoint->receiver_queue);
            new_endpoint->owner_pid = current_process->pid;
            new_endpoint->bound_irq = -1;
            current_process->handle_table[i] = new_endpoint;
            frame->r[0] = i;
            return;
        }
    }
    frame->r[0] = ERR_BUSY;
}

void sys_port_destroy(exception_frame_t *frame)
{
    if (!current_process)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    int handle = (int)frame->r[0];

    // Validate handle
    if (handle < 0 || handle >= MAX_HANDLE_TABLE)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    endpoint_t *ep = current_process->handle_table[handle];
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
        ((exception_frame_t *)(proc->kernel_sp + 9))->r[0] = ERR_DEAD;
        sched_add(proc);
    }

    // Wake all blocked receivers with error
    while (!list_empty(&ep->receiver_queue))
    {
        list_node_t *n = list_pop_front(&ep->receiver_queue);
        process_t *proc = container_of(n, process_t, node);
        proc->ipc_state = IPC_NONE;
        proc->blocked_endpoint = NULL;
        ((exception_frame_t *)(proc->kernel_sp + 9))->r[0] = ERR_DEAD;
        sched_add(proc);
    }

    // Clean up
    kfree(ep);
    current_process->handle_table[handle] = NULL;
    frame->r[0] = 0;
}

void sys_port_grant(exception_frame_t *frame)
{
    int handle = frame->r[0];
    uint32_t pid = frame->r[1];

    // Validate handle
    if (handle < 0 || handle >= MAX_HANDLE_TABLE)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    // look up the target process, find free slot
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

    endpoint_t *ep = current_process->handle_table[handle];
    if (!ep)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    // only owner can grant
    if (ep->owner_pid != current_process->pid)
    {
        frame->r[0] = ERR_NOPERM;
        return;
    }


    // copied this over from port_create, how to edit?
    for (int i = 0; i < MAX_HANDLE_TABLE; i++)
    {
        if (!(grantee->handle_table[i]))
        {
            grantee->handle_table[i] = ep;
            frame->r[0] = 0;
            return;
        }
    }
    return ERR_NOMEM;
}
