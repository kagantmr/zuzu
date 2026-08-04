#include "syscall.h"

#include "kernel/sched/sched.h"
#include "core/log.h"

#include "kernel/ipc/sys_port.h"
#include "kernel/ipc/sys_msg.h"
#include "kernel/ipc/sys_notif.h"
#include "kernel/irq/sys_irq.h"
#include "kernel/mm/sys_mm.h"
#include "kernel/mm/sys_shm.h"
#include "kernel/dev/sys_dev.h"
#include "kernel/proc/kstack.h"
#include "kernel/layout.h"
#include "kernel/proc/sys_proc.h"
#include "kernel/proc/sys_thread.h"
#include "core/panic.h"

#include "kernel/mm/vmm.h"

#include <string.h>
#include <stdbool.h>

extern kernel_layout_t kernel_layout;

typedef void (*syscall_handler_t)(CpuState *);

static syscall_handler_t syscall_table[SYS_MAX + 1] = {
    [SYS_PQUIT] = sys_pquit,
    [SYS_YIELD] = sys_yield,
    [SYS_WAIT] = sys_wait,
    [SYS_GETPID] = sys_getpid,
    [SYS_SLEEP] = sys_sleep,
    [SYS_PSPAWN] = sys_pspawn,
    [SYS_KICKSTART] = sys_kickstart,
    [SYS_PKILL] = sys_pkill,
    [SYS_TMAKE] = sys_tmake,
    [SYS_TJOIN] = sys_tjoin,
    [SYS_TQUIT] = sys_tquit,
    [SYS_MSG_SEND] = SysMsgSend,
    [SYS_MSG_RECV] = SysMsgRecv,
    [SYS_MSG_CALL] = SysMsgCall,
    [SYS_MSG_REPLY] = SysMsgReply,
    [SYS_MSG_LSEND] = SysMsgLsend,
    [SYS_MSG_LCALL] = SysMsgLcall,
    [SYS_MSG_LREPLY] = SysMsgLreply,
    [SYS_WAITANY] = SysWaitAny,
    [SYS_PORT_CREATE] = SysPortCreate,
    [SYS_DESTROY] = SysDestroy,
    [SYS_GRANT] = SysGrant,
    [SYS_NTFN_CREATE] = SysNtfnCreate,
    [SYS_DEV_QUERY] = sys_dev_query,
    [SYS_NTFN_SIGNAL] = SysNtfnSignal,
    [SYS_NTFN_WAIT] = SysNtfnWait,
    [SYS_STAMP] = SysStamp,
    [SYS_MEMMAP] = sys_memmap,
    [SYS_MEMUNMAP] = sys_memunmap,
    [SYS_SHMEM_CREATE] = sys_shm_create,
    [SYS_MEMPROTECT] = sys_memprotect,
    [SYS_ASINJECT] = sys_asinject,
    [SYS_IRQ_BIND] = sys_irq_bind,
    [SYS_IRQ_DONE] = sys_irq_done
};

static bool trap_frame_sane(const CpuState *frame)
{
    uintptr_t p = (uintptr_t)frame;
    if (p == 0 || (p & 0x3u) != 0)
        return false;

    if (kernel_layout.stack_base_va && kernel_layout.stack_top_va &&
        p >= kernel_layout.stack_base_va &&
        p + sizeof(CpuState) <= kernel_layout.stack_top_va)
        return true;

    if (p >= KSTACK_REGION_BASE && p + sizeof(CpuState) <= KSTACK_REGION_TOP)
        return true;

    return false;
}

bool CopyToUser(void *uaddr, const void *kaddr, size_t len)
{
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

bool CopyFromUser(void *kaddr, const void *uaddr, size_t len)
{
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

void __attribute__((hot)) SyscallDispatch(uint8_t svc_num, CpuState *frame)
{
    if (!current_thread)
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
    if (!trap_frame_sane(frame))
    {
        panic("Corrupt trap_frame at syscall dispatch: pid=%u svc=%u frame=%p",
              (unsigned)(current_thread->owner_process ? current_thread->owner_process->pid : 0),
              svc_num, (void *)frame);
    }
    current_thread->trap_frame = frame;

    if (syscall_table[svc_num])
    {
        syscall_handler_t handler = syscall_table[svc_num];
        handler(frame);
        return;
    }
    else
    {
        KERROR("System call 0x%X does not exist", svc_num);
        (*arch_reg(frame, 0)) = ERR_NOSYS;
    }
};
