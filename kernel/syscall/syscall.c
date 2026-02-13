#include "syscall.h"
#include "sys_task.h"
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
    case SYS_GET_PID:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_PROC_SEND:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_PROC_RECV:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_PROC_CALL:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_PROC_REPLY:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_PORT_CREATE:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_PORT_DESTROY:
    {
        frame->r[0] = ERR_NOMATCH;
    } break; 
    case SYS_PORT_GRANT:
    {
        frame->r[0] = ERR_NOMATCH;
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
        KWARN("Syscall no %x does not exist", svc_num);
        frame->r[0] = ERR_NOMATCH;
    } break;
    }
};