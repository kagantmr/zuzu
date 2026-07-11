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

typedef void (*syscall_handler_t)(arch_regs_t*);

static syscall_handler_t syscall_table[SYS_MAX + 1] = {
    [SYS_PQUIT] = pquit,
    [SYS_YIELD] = yield,
    [SYS_WAIT] = wait,
    [SYS_GET_PID] = get_pid,
    [SYS_SLEEP] = sleep,
    [SYS_PSPAWN] = pspawn,
    [SYS_KICKSTART] = kickstart,
    [SYS_PKILL] = pkill,
    [SYS_TMAKE] = tmake,
    [SYS_TJOIN] = tjoin,
    [SYS_TQUIT] = tquit,
    [SYS_MSG_SEND] = msg_send,
    [SYS_MSG_RECV] = msg_recv,
    [SYS_MSG_CALL] = msg_call,
    [SYS_MSG_REPLY] = msg_reply,
    [SYS_MSG_LSEND] = msg_lsend,
    [SYS_MSG_LCALL] = msg_lcall,
    [SYS_MSG_LREPLY] = msg_lreply,
    [SYS_WAITANY] = waitany,
    [SYS_PORT_CREATE] = port_create,
    [SYS_DESTROY] = port_destroy,
    [SYS_GRANT] = grant,
    [SYS_NTFN_CREATE] = ntfn_create,
    [SYS_NTFN_SIGNAL] = ntfn_signal,
    [SYS_NTFN_WAIT] = ntfn_wait,
    [SYS_MEMMAP] = memmap,
    [SYS_MEMUNMAP] = memunmap,
    [SYS_SHM_CREATE] = shm_create,
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

static bool trap_frame_sane(const arch_regs_t *frame)
{
    uintptr_t p = (uintptr_t)frame;
    if (p == 0 || (p & 0x3u) != 0)
        return false;

    if (kernel_layout.stack_base_va && kernel_layout.stack_top_va &&
        p >= kernel_layout.stack_base_va &&
        p + sizeof(arch_regs_t) <= kernel_layout.stack_top_va)
        return true;

    if (p >= KSTACK_REGION_BASE && p + sizeof(arch_regs_t) <= KSTACK_REGION_TOP)
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


void __attribute__((hot)) syscall_dispatch(uint8_t svc_num, arch_regs_t *frame)
{
    //KDEBUG("syscall: pid=%u svc=0x%X frame=%p", current_process ? current_process->pid : 0, svc_num, frame);
    if (!current_thread) { (*arch_reg(frame, 0)) = ERR_BADARG; return; }
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
        (*arch_reg(frame, 0)) = ERR_NOMATCH;
    }
};
