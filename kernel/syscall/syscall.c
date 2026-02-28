#include "syscall.h"
#include "kernel/proc/sys_task.h" 
#include "kernel/sched/sched.h"
#include "core/log.h"

#include "kernel/ipc/sys_port.h" 
#include "kernel/ipc/sys_ipc.h"
#include "kernel/irq/sys_irq.h"
#include "kernel/vmm/sys_vmm.h"

extern process_t *current_process;

void syscall_dispatch(uint8_t svc_num, exception_frame_t *frame)
{
    KDEBUG("Process with PID %d requested SVC #%d", current_process->pid, svc_num);
    current_process->trap_frame = frame;
    switch (svc_num)
    {
    case SYS_TASK_QUIT:
    {
        sys_task_quit(frame);
    } break; 
    case SYS_TASK_YIELD:
    {
        sys_task_yield(frame);
    } break;                   
    case SYS_TASK_SPAWN:
    {
        sys_task_spawn(frame);
    } break; 
    case SYS_TASK_WAIT:
    {
        sys_task_wait(frame);
    } break; 
    case SYS_TASK_SLEEP: {
        sys_task_sleep(frame);
    } break;
    case SYS_GET_PID:
    {
        sys_get_pid(frame);
    } break; 
    case SYS_PROC_SEND:
    {
        proc_send(frame);
    } break; 
    case SYS_PROC_RECV:
    {
        proc_recv(frame);
    } break; 
    case SYS_PROC_CALL:
    {
        proc_call(frame);
    } break; 
    case SYS_PROC_REPLY:
    {
        proc_reply(frame);
    } break; 
    case SYS_PORT_CREATE:
    {
        sys_port_create(frame);
    } break; 
    case SYS_PORT_DESTROY:
    {
        sys_port_destroy(frame);
    } break; 
    case SYS_PORT_GRANT:
    {
        sys_port_grant(frame);
    } break; 
    case SYS_MEMMAP:
    {
        memmap(frame);
    } break; 
    case SYS_MEMUNMAP:
    {
        memunmap(frame);
    } break; 
    case SYS_MEMSHARE:
    {
        memshare(frame);
    } break; 
    case SYS_ATTACH:
    {
        attach(frame);
    } break; 
    case SYS_MAPDEV:
    {
        mapdev(frame);
    } break; 
    case SYS_IRQ_CLAIM:
    {   
        irq_claim(frame);
    } break; 
    case SYS_IRQ_WAIT:
    {
        irq_wait(frame);
    } break; 
    case SYS_IRQ_DONE:
    {
        irq_done(frame);
    } break; 
    case SYS_DUMP:
    {
        frame->r[0] = ERR_NOMATCH;
    } break;
    case SYS_LOG:
    {
        sys_log(frame);
    } break;
    default:
    {
        KERROR("System call 0x%X does not exist", svc_num);
        frame->r[0] = ERR_NOMATCH;
    } break;
    }
};