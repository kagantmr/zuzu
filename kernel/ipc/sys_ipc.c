#include "sys_ipc.h"
#include "kernel/syscall/syscall.h"
#include "endpoint.h"
#include "kernel/sched/sched.h"
#include "kernel/mm/alloc.h"

extern process_t *current_process;

void proc_send(exception_frame_t *frame)
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

    if (!list_empty(&ep->receiver_queue)) { 
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        process_t *rx_proc = container_of(receiver, process_t, node);
        exception_frame_t *rx_frame = (exception_frame_t *)(rx_proc->kernel_sp + 9);
        rx_frame->r[0] = current_process->pid;
        rx_frame->r[1] = frame->r[1];
        rx_frame->r[2] = frame->r[2];
        rx_frame->r[3] = frame->r[3];
        rx_proc->ipc_state = IPC_NONE;
        rx_proc->blocked_endpoint = NULL;
        sched_add(rx_proc);
        frame->r[0] = 0;
    } else {
        current_process->ipc_state = IPC_SENDER;
        current_process->blocked_endpoint = ep;
        list_add_tail(&current_process->node, &ep->sender_queue);
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
    }

}

void proc_recv(exception_frame_t *frame)
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

    if (!list_empty(&ep->sender_queue)) { 
        list_node_t *sender = list_pop_front(&ep->sender_queue);
        process_t *sr_proc = container_of(sender, process_t, node);
        exception_frame_t *sr_frame = (exception_frame_t *)(sr_proc->kernel_sp + 9);
        frame->r[0] = sr_proc->pid;
        frame->r[1] = sr_frame->r[1];
        frame->r[2] = sr_frame->r[2];
        frame->r[3] = sr_frame->r[3];
        sr_proc->ipc_state = IPC_NONE;
        sr_proc->blocked_endpoint = NULL;
        sched_add(sr_proc);
        sr_frame->r[0] = 0;
    } else {
        current_process->ipc_state = IPC_RECEIVER;
        current_process->blocked_endpoint = ep;
        list_add_tail(&current_process->node, &ep->receiver_queue.node);
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
    }
}

void proc_call(exception_frame_t *frame)
{
    frame->r[0] = ERR_NOMATCH;
}
void proc_reply(exception_frame_t *frame)
{
    frame->r[0] = ERR_NOMATCH;
}