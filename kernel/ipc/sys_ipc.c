#include "sys_ipc.h"
#include "kernel/syscall/syscall.h"
#include "endpoint.h"
#include "kernel/sched/sched.h"
#include "core/log.h"
#include "kernel/mm/alloc.h"
#include "lib/mem.h"

extern process_t *current_process;

void proc_send(exception_frame_t *frame)
{
    //KDEBUG("reached proc_send syscall");
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

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        process_t *rx_proc = container_of(receiver, process_t, node);
        exception_frame_t *rx_frame = (exception_frame_t *)(rx_proc->kernel_sp + 9);
        KDEBUG("Sending message from process PID %d to process PID %d", current_process->pid,rx_proc->pid);
        rx_frame->r[0] = current_process->pid;
        rx_frame->r[1] = frame->r[1];
        rx_frame->r[2] = frame->r[2];
        rx_frame->r[3] = frame->r[3];
        rx_proc->ipc_state = IPC_NONE;
        rx_proc->blocked_endpoint = NULL;
        sched_add(rx_proc);
        frame->r[0] = 0;
    }
    else
    {
        current_process->ipc_state = IPC_SENDER;
        current_process->blocked_endpoint = ep;
        list_add_tail(&current_process->node, &ep->sender_queue.node);
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
    }
}

void proc_recv(exception_frame_t *frame)
{
    //KDEBUG("reached proc_recv syscall");
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

    if (!list_empty(&ep->sender_queue))
    {
        //KDEBUG("sender queue NOT empty");
        list_node_t *sender = list_pop_front(&ep->sender_queue);
        process_t *sr_proc = container_of(sender, process_t, node);
        exception_frame_t *sr_frame = (sr_proc->trap_frame);

        // Copy message to receiver 
        KDEBUG("Got message from process PID %d as process PID %d", sr_proc->pid,current_process->pid);
        frame->r[0] = sr_proc->pid;
        frame->r[1] = sr_frame->r[1];
        frame->r[2] = sr_frame->r[2];
        frame->r[3] = sr_frame->r[3];

        if (sr_proc->ipc_state == IPC_SENDER) {
            // wake the sender, it's done
            sr_frame->r[0] = 0;
            sr_proc->ipc_state = IPC_NONE;
            sr_proc->blocked_endpoint = NULL;
            sr_proc->process_state = PROCESS_READY;
            sched_add(sr_proc);
        }
        // IPC_WAITING: sender used call
    }
    else
    {
        KDEBUG("sender queue empty, blocking");
        current_process->ipc_state = IPC_RECEIVER;
        current_process->blocked_endpoint = ep;
        list_add_tail(&current_process->node, &ep->receiver_queue.node);
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
        //KDEBUG("Listener woke from recv, PID %d", current_process->pid);
    }
}

void proc_call(exception_frame_t *frame)
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

    if (!list_empty(&ep->receiver_queue))
    {
        list_node_t *receiver = list_pop_front(&ep->receiver_queue);
        process_t *rx_proc = container_of(receiver, process_t, node);
        exception_frame_t *rx_frame = rx_proc->trap_frame;
        rx_frame->r[0] = current_process->pid;
        rx_frame->r[1] = frame->r[1];
        rx_frame->r[2] = frame->r[2];
        rx_frame->r[3] = frame->r[3];

        rx_proc->ipc_state = IPC_NONE;
        rx_proc->blocked_endpoint = NULL;
        sched_add(rx_proc);

        current_process->process_state = PROCESS_BLOCKED;
        current_process->blocked_endpoint = ep;
        current_process->ipc_state = IPC_WAITING;
        schedule();
    }
    else
    {
        
        current_process->ipc_state = IPC_WAITING;
        current_process->blocked_endpoint = ep;
        list_add_tail(&current_process->node, &ep->sender_queue.node);
        current_process->process_state = PROCESS_BLOCKED;
        schedule();
    }
}

void proc_reply(exception_frame_t *frame)
{
    if (!current_process)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    uint32_t target_pid = frame->r[0];

    process_t *target = process_find_by_pid(target_pid);
    if (!target)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    // Verify it's actually waiting for a reply
    if (target->ipc_state != IPC_WAITING)
    {
        frame->r[0] = ERR_BADARG;
        return;
    }

    // Deliver reply into target's saved frame
    exception_frame_t *target_frame = target->trap_frame;
    target_frame->r[0] = 0;           // success
    target_frame->r[1] = frame->r[1]; // reply payload
    target_frame->r[2] = frame->r[2];
    target_frame->r[3] = frame->r[3];

    // Wake the caller
    target->ipc_state = IPC_NONE;
    target->blocked_endpoint = NULL;
    target->process_state = PROCESS_READY;
    sched_add(target);

    frame->r[0] = 0;
}
