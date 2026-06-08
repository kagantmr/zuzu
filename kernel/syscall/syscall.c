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

extern kernel_layout_t kernel_layout;

typedef void (*syscall_handler_t)(exception_frame_t*);

static syscall_handler_t syscall_table[SYS_MAX + 1] = {
    [SYS_TASK_PQUIT] = pquit,
    [SYS_TASK_YIELD] = yield,
    [SYS_TASK_WAIT] = wait,
    [SYS_GET_PID] = get_pid,
    [SYS_TASK_SLEEP] = sleep,
    [SYS_TASK_PSPAWN] = pspawn,
    [SYS_TASK_KICKSTART] = kickstart,
    [SYS_TASK_KILL] = kill,
    [SYS_TASK_TMAKE] = tmake,
    [SYS_TASK_TJOIN] = tjoin,
    [SYS_TASK_TQUIT] = tquit,
    [SYS_PROC_SEND] = proc_send,
    [SYS_PROC_RECV] = proc_recv,
    [SYS_PROC_CALL] = proc_call,
    [SYS_PROC_REPLY] = proc_reply,
    [SYS_PROC_SENDX] = proc_sendx,
    [SYS_PROC_CALLX] = proc_callx,
    [SYS_PROC_REPLYX] = proc_replyx,
    [SYS_PROC_RECVANY] = proc_recvany,
    [SYS_EP_CREATE] = port_create,
    [SYS_CAP_DESTROY] = port_destroy,
    [SYS_CAP_GRANT] = port_grant,
    [SYS_NTFN_CREATE] = ntfn_create,
    [SYS_NTFN_SIGNAL] = ntfn_signal,
    [SYS_NTFN_WAIT] = ntfn_wait,
    [SYS_MEMMAP] = memmap,
    [SYS_MEMUNMAP] = memunmap,
    [SYS_MEMSHARE] = memshare,
    [SYS_ATTACH] = attach,
    [SYS_MAPDEV] = mapdev,
    [SYS_DETACH] = detach,
    [SYS_QUERYDEV] = querydev,
    [SYS_MPROTECT] = mprotect,
    [SYS_ASINJECT] = asinject,
    [SYS_IRQ_CLAIM] = irq_claim, 
    [SYS_IRQ_BIND] = irq_bind, 
    [SYS_IRQ_DONE] = irq_done
};

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
    if (!current_thread || !current_thread->owner_process || !current_thread->owner_process->as || !uaddr || !kaddr)
        return false;
    if (!validate_user_ptr((uintptr_t)uaddr, len))
        return false;
    if (!fault_in_pages(current_thread->owner_process->as, (uintptr_t)uaddr, len, true))
        return false;

    memcpy(uaddr, kaddr, len);
    return true;
}

bool copy_from_user(void *kaddr, const void *uaddr, size_t len) {
    if (len == 0)
        return true;
    if (!current_thread || !current_thread->owner_process || !current_thread->owner_process->as || !uaddr || !kaddr)
        return false;
    if (!validate_user_ptr((uintptr_t)uaddr, len))
        return false;
    if (!fault_in_pages(current_thread->owner_process->as, (uintptr_t)uaddr, len, false))
        return false;

    memcpy(kaddr, uaddr, len);
    return true;
}


void __attribute__((hot)) syscall_dispatch(uint8_t svc_num, exception_frame_t *frame)
{
    //KDEBUG("syscall: pid=%u svc=0x%X frame=%p", current_process ? current_process->pid : 0, svc_num, frame);
    if (!current_thread) { frame->r[0] = ERR_BADARG; return; }
    if (!trap_frame_sane(frame)) {
        KERROR("bad syscall frame: pid=%u svc=%u frame=%p", current_thread->owner_process ? current_thread->owner_process->pid : 0u, svc_num, frame);
        panic("Corrupt trap_frame at syscall dispatch");
    }
    current_thread->trap_frame = frame;

    if (syscall_table[svc_num]) {
        syscall_handler_t handler = syscall_table[svc_num];
        handler(frame);
        return;
    } else {
        KERROR("System call 0x%X does not exist", svc_num);
        frame->r[0] = ERR_NOMATCH;
    }
};
