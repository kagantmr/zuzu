#include "core/ensure.h"
#include "core/panic.h"
#include "kernel/bench.h"
#include "kernel/layout.h"
#include "kernel/space/space.h"
#include "kernel/task/kstack.h"
#include "svc.h"
#include "svc_nums.h"
#include <compiler.h>

extern RamLayout kernel_layout;

typedef void (*SvcEntry)(CpuState *);

#ifdef CONFIG_ZUZU_BENCH
static void SvcBenchControl(CpuState *frame)
{
    switch ((BenchVerb)(*ArchGetFromFrame(frame, 0))) {
    case BENCH_RESET:
        BenchResetAll();
        break;
    case BENCH_DUMP:
        BenchDumpAll();
        break;
    default:
        ArchSetInFrame(frame, 0, ERR_BADARG);
        return;
    }
    ArchSetInFrame(frame, 0, ZUZU_OK);
}
#endif

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
#ifdef CONFIG_ZUZU_BENCH
    [SVC_BENCH] = SvcBenchControl,
#endif
};

#ifdef CONFIG_ZUZU_BENCH
#define SVC_BENCH_VERBS 16u

static BenchStat svc_bench[SVC_TOTAL_COUNT][SVC_BENCH_VERBS];

static const char *const svc_bench_names[SVC_TOTAL_COUNT][SVC_BENCH_VERBS] = {
    [SVC_CREATE] = {[OBJECT_TASK] = "create.task",
                    [OBJECT_SPACE] = "create.space",
                    [OBJECT_PORT] = "create.port",
                    [OBJECT_EVENT] = "create.event",
                    [OBJECT_MEMORY] = "create.memory"},
    [SVC_MANAGEHANDLE] = {[MNGHNDL_DUPLICATE] = "handle.duplicate",
                          [MNGHNDL_RESTRICT] = "handle.restrict",
                          [MNGHNDL_CLOSE] = "handle.close",
                          [MNGHNDL_QUERY] = "handle.query",
                          [MNGHNDL_DESTROY] = "handle.destroy",
                          [MNGHNDL_GRANT] = "handle.grant",
                          [MNGHNDL_IRQ_REARM] = "handle.irq_rearm"},
    [SVC_SIGNAL] = {[0] = "signal"},
    [SVC_BIND] = {[EVENT_MEMMGMT] = "bind.memmgmt",
                  [EVENT_IRQ] = "bind.irq",
                  [EVENT_PORT] = "bind.port",
                  [EVENT_TASK] = "bind.task",
                  [EVENT_SPACE] = "bind.space"},
    [SVC_MANAGEMEMORY] = {[MNGMEM_MAP] = "mem.map",
                          [MNGMEM_UNMAP] = "mem.unmap",
                          [MNGMEM_PROTECT] = "mem.protect",
                          [MNGMEM_INJECTOBJ] = "mem.injectobj"},
    [SVC_MANAGETASK] = {[MNGTASK_START] = "task.start",
                        [MNGTASK_KILL] = "task.kill",
                        [MNGTASK_SET_PRIORITY] = "task.set_prio",
                        [MNGTASK_SET_MAX_PRIO] = "task.set_max_prio",
                        [MNGTASK_SET_TIMESLICE] = "task.set_timeslice",
                        [MNGTASK_SUSPEND] = "task.suspend",
                        [MNGTASK_RESUME] = "task.resume",
                        [MNGTASK_GET_REGS] = "task.get_regs",
                        [MNGTASK_SET_REGS] = "task.set_regs"},
};

static BenchStat *SvcBenchSlot(Svc svc_num, CpuState *frame)
{
    uint32_t verb = 0;
    switch (svc_num) {
    case SVC_CREATE:
    case SVC_BIND:
    case SVC_MANAGEMEMORY:
        verb = (uint32_t)(*ArchGetFromFrame(frame, 0));
        break;
    case SVC_MANAGEHANDLE:
    case SVC_MANAGETASK:
        verb = (uint32_t)(*ArchGetFromFrame(frame, 1));
        break;
    default:
        break;
    }
    if (verb >= SVC_BENCH_VERBS || !svc_bench_names[svc_num][verb])
        return NULL;
    BenchStat *st = &svc_bench[svc_num][verb];
    if (!st->name)
        st->name = svc_bench_names[svc_num][verb];
    return st;
}
#endif

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

    if (likely(svc_num < SVC_TOTAL_COUNT && svc_table[svc_num])) {
#ifdef CONFIG_ZUZU_BENCH
        BenchStat *bench = SvcBenchSlot(svc_num, frame);
        uint32_t bench_start = bench ? BENCH_BEGIN() : 0;
#endif
        svc_table[svc_num](frame);
#ifdef CONFIG_ZUZU_BENCH
        if (bench)
            bench_record(bench, ArchMeasure() - bench_start);
#endif
    } else
        ArchSetInFrame(frame, 0, ERR_NOSYS);
    return;
PanicOnWeirdFrame:
    panic("Corrupt trap_frame at syscall dispatch: spid=%u svc=%u frame=%p",
          (unsigned)(CURRENT_SPACE ? CURRENT_SPACE->spid : 0), svc_num, (void *)frame);
    __builtin_unreachable();
}