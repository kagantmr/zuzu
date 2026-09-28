#include "svc.h"

#include "kernel/sched/sched.h"
#include "core/log.h"
#include "core/ensure.h"

#include "kernel/space/space.h"
#include "kernel/task/kstack.h"
#include "kernel/layout.h"
#include "core/panic.h"

#include "kernel/bench.h"

#include <compiler.h>
#include <string.h>
#include <stdbool.h>

extern ZuzuRamLayout kernel_layout;

#ifdef CONFIG_ZUZU_BENCH
BENCH_STAT(g_bench_copytouser_walk, "CopyToUser: VmmCheckUserFault");
BENCH_STAT(g_bench_copytouser_copy, "CopyToUser: memcpy");
BENCH_STAT(g_bench_copyfromuser_walk, "CopyFromUser: VmmCheckUserFault");
BENCH_STAT(g_bench_copyfromuser_copy, "CopyFromUser: memcpy");
#endif

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
