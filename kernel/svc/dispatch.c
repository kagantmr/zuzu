#include "core/ensure.h"
#include "core/panic.h"
#include "kernel/layout.h"
#include "kernel/space/space.h"
#include "kernel/task/kstack.h"
#include "svc.h"
#include "svc_nums.h"
#include <compiler.h>

extern RamLayout kernel_layout;

typedef void (*SvcEntry)(CpuState *);

static SvcEntry svc_table[SVC_TOTAL_COUNT] = {
    [SVC_QUIT] = SvcQuit,
    [SVC_YIELD] = SvcYield,
#ifdef DEBUG
    [SVC_LOG] = SvcDebugLog, /* defined in svc_log.c; DEBUG builds only */
#else
    [SVC_LOG] = NULL,
#endif
    [SVC_SLEEP] = SvcSleep,
    [SVC_CREATE] = SvcCreate,
    [SVC_CALL] = SvcCall,
    [SVC_REPLY] = SvcReply,
    [SVC_WAITON] = SvcWaitOn,
    [SVC_MANAGEHANDLE] = SvcManageHandle,
    [SVC_SIGNAL] = SvcSignal,
    [SVC_BIND] = SvcBind,
    [SVC_MANAGEMEMORY] = SvcManageMemory,
    [SVC_MANAGETASK] = SvcManageTask,
};

static __hot bool IsNormalFrame(const CpuState *frame)
{
    uintptr_t p = (uintptr_t)frame;
    if (unlikely(p == 0 || (p & 0x3U) != 0))
        return false;

    if (likely(kernel_layout.stack_base_va && kernel_layout.stack_top_va &&
               p >= kernel_layout.stack_base_va &&
               p + sizeof(CpuState) <= kernel_layout.stack_top_va))
        return true;

    if (p >= KSTACK_REGION_BASE && p + sizeof(CpuState) <= KSTACK_REGION_TOP)
        return true;

    return false;
}

void __hot SvcDispatch(Svc svc_num, CpuState *frame)
{
    ENSURE_ERR(frame, current_task, ERR_BADARG);
    ENSURE_GOTO(IsNormalFrame(frame), PanicOnWeirdFrame);

    current_task->trap_frame = frame;

    if (likely(svc_num < SVC_TOTAL_COUNT && svc_table[svc_num]))
        svc_table[svc_num](frame);
    else
        ArchSetInFrame(frame, 0, ERR_NOSYS);
    return;
PanicOnWeirdFrame:
    panic("Corrupt trap_frame at syscall dispatch: spid=%u svc=%u frame=%p",
          (unsigned)(CURRENT_SPACE ? CURRENT_SPACE->spid : 0), svc_num, (void *)frame);
    __builtin_unreachable();
}