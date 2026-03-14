#include "sys_port.h"
#include "kernel/syscall/syscall.h"
#include "endpoint.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"
#include "kernel/proc/process.h"

extern process_t *current_process;
extern process_t *process_table[MAX_PROCESSES];
endpoint_t *nametable_endpoint;

void port_create(exception_frame_t *frame)
{
    if (!current_process)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    for (int i = 0; i < MAX_HANDLE_TABLE; i++)
    {
        if (current_process->handle_table[i].type == HANDLE_FREE)
        {
            endpoint_t *new_endpoint = kmalloc(sizeof(endpoint_t));
            if (!new_endpoint)
            {
                frame->r[0] = ERR_NOMEM;
                return;
            }
            if (current_process->flags & PROC_FLAG_NAMETABLE && !nametable_endpoint) {
                nametable_endpoint = new_endpoint;
                /* Inject NT port into all processes spawned before nametable existed */
                for (int j = 0; j < MAX_PROCESSES; j++) {
                    process_t *p = process_table[j];
                    if (p && p != current_process &&
                        p->handle_table[0].type == HANDLE_FREE) {
                        p->handle_table[0].ep       = nametable_endpoint;
                        p->handle_table[0].grantable = true;
                        p->handle_table[0].type     = HANDLE_ENDPOINT;
                    }
                }
            }
            // list_init(&new_endpoint->node);
            list_init(&new_endpoint->sender_queue);
            list_init(&new_endpoint->receiver_queue);
            new_endpoint->owner_pid = current_process->pid;
            new_endpoint->bound_irq = -1;
            current_process->handle_table[i].ep = new_endpoint;
            current_process->handle_table[i].grantable = true;
            current_process->handle_table[i].type = HANDLE_ENDPOINT;


            frame->r[0] = i;
            return;
        }
    }
    frame->r[0] = ERR_BUSY;
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
    if (handle < 0 || handle >= MAX_HANDLE_TABLE)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    endpoint_t *ep = current_process->handle_table[handle].ep;
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
    current_process->handle_table[handle].ep = NULL;
    current_process->handle_table[handle].type = HANDLE_FREE;
    frame->r[0] = 0;
}

void port_grant(exception_frame_t *frame)
{
    int handle = frame->r[0];
    uint32_t pid = frame->r[1];

    // Validate handle
    if (handle < 0 || handle >= MAX_HANDLE_TABLE)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    handle_entry_t entry = current_process->handle_table[handle];
    if (entry.type == HANDLE_FREE) {
        frame->r[0] = ERR_BADARG;
        return;
    }

    if (!entry.grantable) {
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

    // Find free slot in grantee's table
    for (int i = 0; i < MAX_HANDLE_TABLE; i++)
    {
        if (grantee->handle_table[i].type == HANDLE_FREE)
        {
            grantee->handle_table[i] = entry;
            grantee->handle_table[i].grantable = (grantee->pid == NAMETABLE_PID);
            if (entry.type == HANDLE_SHMEM && entry.shm) {
                entry.shm->ref_count++;
            }
            frame->r[0] = i;
            return;
        }
    }
    frame->r[0] = ERR_NOMEM;
}