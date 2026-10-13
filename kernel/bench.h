// kernel/bench.h - PMCCNTR min/avg/max instrumentation for CONFIG_ZUZU_BENCH builds.
//
// Each measurement point declares a BENCH_STAT() at file scope and brackets the
// code under test with BENCH_BEGIN()/BENCH_END(). Nothing is printed while
// sampling: userland resets the stats and dumps them on demand through
// SVC_BENCH (BenchReset()/BenchDump()). Nothing here exists outside
// CONFIG_ZUZU_BENCH builds.

#ifndef KERNEL_BENCH_H
#define KERNEL_BENCH_H

#ifdef CONFIG_ZUZU_BENCH

#include <arch/cycles.h>
#include <stdbool.h>
#include <types.h>

typedef struct BenchStat {
    const char *name;
    uint32_t min;
    uint32_t max;
    uint64_t sum;
    uint32_t count;
    bool linked;
    struct BenchStat *next;
} BenchStat;

void BenchLink(BenchStat *s);
void BenchDumpAll(void);
void BenchResetAll(void);

#define BENCH_STAT(varname, label) static BenchStat varname = {.name = (label)}

static inline void bench_record(BenchStat *s, uint32_t cycles)
{
    if (!s->linked)
        BenchLink(s);
    if (s->count == 0 || cycles < s->min)
        s->min = cycles;
    if (cycles > s->max)
        s->max = cycles;
    s->sum += cycles;
    s->count++;
}

#define BENCH_BEGIN() ArchMeasure()
#define BENCH_END(stat, start_val) bench_record(&(stat), ArchMeasure() - (start_val))

#endif /* CONFIG_ZUZU_BENCH */

#endif /* KERNEL_BENCH_H */
