/**
 * @file bench.c
 * @brief Global list of benchmark statistics, with dump and reset.
 */

#include "core/kprintf.h"
#include "kernel/bench.h"

/** @brief Head of the list of every statistic that has recorded a sample. */
static BenchStat *bench_list;

void BenchLink(BenchStat *s)
{
    s->next = bench_list;
    bench_list = s;
    s->linked = true;
}

void BenchDumpAll(void)
{
    for (BenchStat *s = bench_list; s; s = s->next) {
        if (!s->count)
            continue;
        uint64_t avg_x100 = (s->sum * 100) / s->count;
        kprintf("[BENCH] %-32s n=%-7u min=%-8u avg=%u.%02u max=%-8u\n", s->name, s->count, s->min,
                (uint32_t)(avg_x100 / 100), (uint32_t)(avg_x100 % 100), s->max);
    }
}

void BenchResetAll(void)
{
    for (BenchStat *s = bench_list; s; s = s->next) {
        s->min = s->max = s->count = 0;
        s->sum = 0;
    }
}
