#include "syscall.h"

uint32_t syscall_dispatch(uint8_t svc_num)
{
    switch (svc_num)
    {
    case SYS_TASK_QUIT:
    {
    } break; 
    case SYS_TASK_YIELD:
    {
    } break;                   
    case SYS_TASK_SPAWN:
    {
    } break; 
    case SYS_TASK_WAIT:
    {
    } break; 
    case SYS_GET_PID:
    {
    } break; 
    case SYS_PROC_SEND:
    {
    } break; 
    case SYS_PROC_RECV:
    {
    } break; 
    case SYS_PROC_CALL:
    {
    } break; 
    case SYS_PROC_REPLY:
    {
    } break; 
    case SYS_PORT_CREATE:
    {
    } break; 
    case SYS_PORT_DESTROY:
    {
    } break; 
    case SYS_PORT_GRANT:
    {
    } break; 
    case SYS_MMAP:
    {
    } break; 
    case SYS_MUNMAP:
    {
    } break; 
    case SYS_MSHARE:
    {
    } break; 
    case SYS_ATTACH:
    {
    } break; 
    case SYS_MAPDEV:
    {
    } break; 
    case SYS_IRQ_CLAIM:
    {
    } break; 
    case SYS_IRQ_WAIT:
    {
    } break; 
    case SYS_IRQ_DONE:
    {
    } break; 
    case SYS_LOG:
    {
    } break;
    case SYS_DUMP:
    {
    } break;
    default:
    {
        return ERR_NOMATCH;
    } break;
    }
};