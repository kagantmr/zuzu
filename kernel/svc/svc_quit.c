#include "kernel/bench.h"
#include "kernel/sched/sched.h"
#include "kernel/task/task.h"
#include "svc.h"

#define LOG_FMT(fmt) "(SvcQuit) " fmt
#include <util/log.h>

#ifdef CONFIG_ZUZU_BENCH
BENCH_STAT(g_bench_quit_terminate, "quit: TaskTerminate");
#endif

void SvcQuit(CpuState *frame)
{
    int32_t exit_status = (int32_t)(*ArchGetFromFrame(frame, 0));

#ifdef CONFIG_ZUZU_BENCH
    uint32_t bench_start = BENCH_BEGIN();
#endif
    TaskTerminate(current_task, exit_status);
#ifdef CONFIG_ZUZU_BENCH
    BENCH_END(g_bench_quit_terminate, bench_start);
#endif
    KDEBUG("Task %d exited with status %d", current_task->tid, exit_status);

    Schedule();
}
