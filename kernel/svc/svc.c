#include "svc.h"

#include "kernel/sched/sched.h"
#include "core/log.h"
#include "core/ensure.h"

#include "kernel/space/space.h"
#include "kernel/task/kstack.h"
#include "kernel/layout.h"
#include "core/panic.h"

#include "zuzu/syscall_nums.h"
#include "kernel/bench.h"

#include <compiler.h>
#include <string.h>
#include <stdbool.h>

extern kernel_layout_t kernel_layout;

#ifdef CONFIG_ZUZU_BENCH
BENCH_STAT(g_bench_copytouser_walk, "CopyToUser: VmmCheckUserFault");
BENCH_STAT(g_bench_copytouser_copy, "CopyToUser: memcpy");
BENCH_STAT(g_bench_copyfromuser_walk, "CopyFromUser: VmmCheckUserFault");
BENCH_STAT(g_bench_copyfromuser_copy, "CopyFromUser: memcpy");
#endif

typedef void (*SvcEntry)(CpuState *);

#ifdef DEBUG
static void SvcDebugLog(CpuState *frame);
#endif

static SvcEntry svc_table[SVC_TOTAL_COUNT] = {
    [SVC_QUIT] = SvcQuit,
    [SVC_YIELD] = SvcYield,
    #ifdef DEBUG
        [SVC_LOG] = SvcDebugLog, /* defined below; DEBUG builds only */
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
    [SVC_MANAGEMEMORY] = SvcManageMemory
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

bool __hot CopyToUser(void *restrict uaddr, const void *restrict kaddr, size_t len)
{
    if (len == 0)
        return true;
    if (!current_task || !CURRENT_SPACE || !CURRENT_SPACE->as || !uaddr || !kaddr)
        return false;
    if (!IsUserPtrNormal((uintptr_t)uaddr, len))
        return false;
#ifdef CONFIG_ZUZU_BENCH
    uint32_t bench_start = BENCH_BEGIN();
#endif
    if (!VmmCheckUserFault(CURRENT_SPACE->as, (uintptr_t)uaddr, len, true))
        return false;
#ifdef CONFIG_ZUZU_BENCH
    BENCH_END(g_bench_copytouser_walk, bench_start);
    bench_start = BENCH_BEGIN();
#endif

    memcpy(uaddr, kaddr, len);
#ifdef CONFIG_ZUZU_BENCH
    BENCH_END(g_bench_copytouser_copy, bench_start);
#endif
    return true;
}

bool __hot CopyFromUser(void *restrict kaddr, const void *restrict uaddr, size_t len)
{
    if (len == 0)
        return true;
    if (!current_task || !CURRENT_SPACE || !CURRENT_SPACE->as || !uaddr || !kaddr)
        return false;
    if (!IsUserPtrNormal((VirtAddr)uaddr, len))
        return false;
#ifdef CONFIG_ZUZU_BENCH
    uint32_t bench_start = BENCH_BEGIN();
#endif
    if (!VmmCheckUserFault(CURRENT_SPACE->as, (VirtAddr)uaddr, len, false))
        return false;
#ifdef CONFIG_ZUZU_BENCH
    BENCH_END(g_bench_copyfromuser_walk, bench_start);
    bench_start = BENCH_BEGIN();
#endif

    memcpy(kaddr, uaddr, len);
#ifdef CONFIG_ZUZU_BENCH
    BENCH_END(g_bench_copyfromuser_copy, bench_start);
#endif
    return true;
}

#ifdef DEBUG
#define SYSLOG_MAX 240u
static void SvcDebugLog(CpuState *frame)
{
    VirtAddr uptr = (VirtAddr)(*ArchGetFromFrame(frame, 0));
    size_t len = (size_t)(*ArchGetFromFrame(frame, 1));
    char buf[SYSLOG_MAX + 1];

    if (len > SYSLOG_MAX)
        len = SYSLOG_MAX;
    if (len == 0 || !CopyFromUser(buf, (const void *)uptr, len)) {
        ArchSetInFrame(frame, 0, ERR_BADPTR);
        return;
    }
    buf[len] = '\0';
    kprintf("[udbg spid=%u] %s\n",
            (unsigned)(CURRENT_SPACE ? CURRENT_SPACE->spid : 0), buf);
    ArchSetInFrame(frame, 0, 0);
}
#endif /* DEBUG */

void __hot SvcDispatch(Svc svc_num, CpuState *frame)
{
    ENSURE_ERR(frame, current_task, ERR_BADARG);
    ENSURE_GOTO(IsNormalFrame(frame), PanicOnWeirdFrame);
    
    current_task->trap_frame = frame;

    if (likely(svc_table[svc_num]))
        svc_table[svc_num](frame);
    else
        ArchSetInFrame(frame, 0, ERR_NOSYS);
    return;
PanicOnWeirdFrame:
    panic("Corrupt trap_frame at syscall dispatch: pid=%u svc=%u frame=%p",
            (unsigned)(CURRENT_SPACE ? CURRENT_SPACE->spid : 0),
        svc_num, (void *)frame);
    __builtin_unreachable();
}
