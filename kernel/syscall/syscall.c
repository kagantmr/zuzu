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

#include <compiler.h>
#include <string.h>
#include <stdbool.h>

extern kernel_layout_t kernel_layout;

typedef void (*SyscallEntryPoint)(CpuState *);

static SyscallEntryPoint SyscallTable[SYS_MAX + 1] = {
    [SYS_PQUIT] = SysPQuit,
    [SYS_YIELD] = SysYield,
    [SYS_WAIT] = SysWait,
    [SYS_GETPID] = SysGetPid,
    [SYS_SLEEP] = SysSleep,
    [SYS_PSPAWN] = SysPSpawn,
    [SYS_KICKSTART] = SysKickstart,
    [SYS_PKILL] = SysPKill,
    [SYS_TMAKE] = SysTMake,
    [SYS_TJOIN] = SysTJoin,
    [SYS_TQUIT] = SysTQuit,
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
    [SYS_DEV_QUERY] = SysDevQuery,
    [SYS_NTFN_SIGNAL] = SysNtfnSignal,
    [SYS_NTFN_WAIT] = SysNtfnWait,
    [SYS_STAMP] = SysStamp,
    [SYS_MEMMAP] = SysMemMap,
    [SYS_MEMUNMAP] = SysMemUnmap,
    [SYS_SHMEM_CREATE] = SysShmCreate,
    [SYS_MEMPROTECT] = SysMemProtect,
    [SYS_ASINJECT] = SysAsInject,
    [SYS_IRQ_BIND] = SysIrqBind,
    [SYS_IRQ_DONE] = SysIrqDone
};

/* Runs on every syscall. The two "in bounds" checks below are the normal
 * case (a syscall from an intact, correctly-placed kernel stack); a miss
 * on both means a corrupted trap frame, which is fatal (panic below) --
 * genuinely rare, so the false-return tail is the cold path. */
static __hot bool trap_frame_sane(const CpuState *frame)
{
    uintptr_t p = (uintptr_t)frame;
    if (unlikely(p == 0 || (p & 0x3u) != 0))
        return false;

    if (likely(kernel_layout.stack_base_va && kernel_layout.stack_top_va &&
	       p >= kernel_layout.stack_base_va &&
	       p + sizeof(CpuState) <= kernel_layout.stack_top_va))
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

void __attribute__((hot)) SyscallDispatch(Svc svc_num, CpuState *frame)
{
    if (unlikely(!current_thread))
    {
        (*arch_reg(frame, 0)) = ERR_BADARG;
        return;
    }
    if (unlikely(!trap_frame_sane(frame)))
    {
        panic("Corrupt trap_frame at syscall dispatch: pid=%u svc=%u frame=%p",
              (unsigned)(current_thread->owner_process ? current_thread->owner_process->pid : 0),
              svc_num, (void *)frame);
    }
    current_thread->trap_frame = frame;

    if (likely(SyscallTable[svc_num]))
    {
        SyscallEntryPoint handler = SyscallTable[svc_num];
        handler(frame);
        return;
    }
    else
    {
        KERROR("System call 0x%X does not exist", svc_num);
        (*arch_reg(frame, 0)) = ERR_NOSYS;
    }
};
