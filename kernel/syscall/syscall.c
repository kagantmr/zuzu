#include "syscall.h"
#include "kernel/proc/sys_task.h" 
#include "kernel/sched/sched.h"
#include "core/log.h"

#include "kernel/ipc/sys_port.h" 
#include "kernel/ipc/sys_ipc.h"
#include "kernel/irq/sys_irq.h"
#include "kernel/vmm/sys_vmm.h"
#include "kernel/dev/sys_dev.h"

extern process_t *current_process;

void syscall_dispatch(uint8_t svc_num, exception_frame_t *frame)
{

    if (!current_process) { frame->r[0] = ERR_BADARG; return; }
    current_process->trap_frame = frame;
    switch (svc_num)
    {
    case SYS_TASK_QUIT:
    {
        quit(frame);
    } break; 
    case SYS_TASK_YIELD:
    {
        yield(frame);
    } break;                   
    case SYS_TASK_SPAWN:
    {
        spawn(frame);
    } break; 
    case SYS_TASK_WAIT:
    {
        wait(frame);
    } break; 
    case SYS_TASK_SLEEP: {
        sleep(frame);
    } break;
    case SYS_GET_PID:
    {
        get_pid(frame);
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
        port_create(frame);
    } break; 
    case SYS_PORT_DESTROY:
    {
        port_destroy(frame);
    } break; 
    case SYS_PORT_GRANT:
    {
        port_grant(frame);
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
    case SYS_DETACH:
    {
        detach(frame);
    } break; 
    case SYS_GETDEV:
    {
        getdev(frame);
    } break;
    case SYS_ENUMDEV:
    {
        enumdev(frame);
    } break;
    case SYS_IRQ_CLAIM:
    {   
        irq_claim(frame);
    } break; 
    case SYS_IRQ_BIND:
    {
        irq_bind(frame);
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
    case SYS_PMM_GETFREE:
    {
        sys_pmm_getfree(frame);
    } break;  
    default:
    {
        KERROR("System call 0x%X does not exist", svc_num);
        frame->r[0] = ERR_NOMATCH;
    } break;
    }
};
