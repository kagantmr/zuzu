#include "syscall.h"
#include "kernel/sched/sys_task.h" 
#include "kernel/ipc/sys_port.h" 
#include "kernel/ipc/sys_ipc.h"
#include "core/log.h"

void syscall_dispatch(uint8_t svc_num, exception_frame_t *frame)
{
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
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_TASK_WAIT:
    {
        frame->r[0] = ERR_NOMATCH;
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
    case SYS_MMAP:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_MUNMAP:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_MSHARE:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_ATTACH:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_MAPDEV:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_IRQ_CLAIM:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_IRQ_WAIT:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_IRQ_DONE:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_LOG:
    {
        sys_log(frame);
    } break;
    case SYS_DUMP:
    {
        frame->r[0] = ERR_NOMATCH;
    } break;
    default:
    {
        KWARN("System call 0x%X does not exist", svc_num);
        frame->r[0] = ERR_NOMATCH;
    } break;
    }
};