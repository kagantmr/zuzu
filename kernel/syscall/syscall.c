#include "syscall.h"
#include "kernel/proc/sys_task.h" 
#include "kernel/sched/sched.h"
#include "core/log.h"

#include "kernel/ipc/sys_port.h" 
#include "kernel/ipc/sys_ipc.h"
#include "kernel/ipc/sys_notif.h"
#include "kernel/irq/sys_irq.h"
#include "kernel/mm/sys_mm.h"
#include "kernel/dev/sys_dev.h"
#include "kernel/proc/kstack.h"
#include "kernel/layout.h"
#include "core/panic.h"

#include "kernel/mm/vmm.h"

#include <mem.h>
#include <stdbool.h>

extern process_t *current_process;
extern kernel_layout_t kernel_layout;

#define KSTACK_REGION_TOP (KSTACK_REGION_BASE + (64u * 0x2000u))

static bool trap_frame_sane(const exception_frame_t *frame)
{
    uintptr_t p = (uintptr_t)frame;
    if (p == 0 || (p & 0x3u) != 0)
        return false;

    if (kernel_layout.stack_base_va && kernel_layout.stack_top_va &&
        p >= kernel_layout.stack_base_va &&
        p + sizeof(exception_frame_t) <= kernel_layout.stack_top_va)
        return true;

    if (p >= KSTACK_REGION_BASE && p + sizeof(exception_frame_t) <= KSTACK_REGION_TOP)
        return true;

    return false;
}

bool copy_to_user(void *uaddr, const void *kaddr, size_t len) {
    if (len == 0)
        return true;
    if (!current_process || !current_process->as || !uaddr || !kaddr)
        return false;
    if (!validate_user_ptr((uintptr_t)uaddr, len))
        return false;
    if (!fault_in_pages(current_process->as, (uintptr_t)uaddr, len, true))
        return false;

    memcpy(uaddr, kaddr, len);
    return true;
}

bool copy_from_user(void *kaddr, const void *uaddr, size_t len) {
    if (len == 0)
        return true;
    if (!current_process || !current_process->as || !uaddr || !kaddr)
        return false;
    if (!validate_user_ptr((uintptr_t)uaddr, len))
        return false;
    if (!fault_in_pages(current_process->as, (uintptr_t)uaddr, len, false))
        return false;

    memcpy(kaddr, uaddr, len);
    return true;
}

void syscall_dispatch(uint8_t svc_num, exception_frame_t *frame)
{
    //KDEBUG("syscall: pid=%u svc=0x%X frame=%p", current_process ? current_process->pid : 0, svc_num, frame);
    if (!current_process) { frame->r[0] = ERR_BADARG; return; }
    if (!trap_frame_sane(frame)) {
        KERROR("bad syscall frame: pid=%u svc=%u frame=%p", current_process->pid, svc_num, frame);
        panic("Corrupt trap_frame at syscall dispatch");
    }
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
    case SYS_PROC_SENDX:
    {
        proc_sendx(frame);
    } break;
    case SYS_PROC_CALLX:
    {
        proc_callx(frame);
    } break;
    case SYS_PROC_REPLYX:
    {
        proc_replyx(frame);
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
    case SYS_NTFN_CREATE:
    {
        ntfn_create(frame);
    } break;
    case SYS_NTFN_SIGNAL:
    {
        ntfn_signal(frame);
    } break;
    case SYS_NTFN_WAIT:
    {
        ntfn_wait(frame);
    } break;
    case SYS_NTFN_POLL:
    {
        ntfn_poll(frame);
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
    case SYS_QUERYDEV:
    {
        querydev(frame);
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
    default:
    {
        KERROR("System call 0x%X does not exist", svc_num);
        frame->r[0] = ERR_NOMATCH;
    } break;
    }
};
