// kernel/bench.h - PMCCNTR min/avg/max instrumentation for ZUZU_BENCH builds.
//
// Each measurement point declares a BENCH_STAT() at file scope, brackets the
// code under test with BENCH_BEGIN()/BENCH_END(), and gets a one-line
// min/avg/max summary printed to the kernel console the moment the warm-up
// is discarded and BENCH_ITERS samples have been collected. Nothing here
// exists outside ZUZU_BENCH builds.

#ifndef ZUZU_KERNEL_BENCH_H
#define ZUZU_KERNEL_BENCH_H

#ifdef ZUZU_BENCH

#include <arch/cycles.h>
#include <stdbool.h>
#include <zuzu/types.h>

#include "core/kprintf.h"

#define BENCH_WARMUP_ITERS 500u
#define BENCH_ITERS        100000u

typedef struct {
	const char *name;
	uint32_t min;
	uint32_t max;
	uint64_t sum;
	uint32_t warmup_left;
	uint32_t count;
	bool reported;
} BenchStat;

#define BENCH_STAT(varname, label) \
	static BenchStat varname = { .name = (label), .warmup_left = BENCH_WARMUP_ITERS }

static inline void bench_record(BenchStat *s, uint32_t cycles)
{
	if (s->reported)
		return;
	if (s->warmup_left > 0) {
		s->warmup_left--;
		return;
	}
	if (s->count == 0 || cycles < s->min)
		s->min = cycles;
	if (cycles > s->max)
		s->max = cycles;
	s->sum += cycles;
	s->count++;
	if (s->count == BENCH_ITERS) {
		uint64_t avg_x100 = (s->sum * 100) / s->count;
		kprintf("[BENCH] %-32s min=%-8u avg=%u.%02u max=%-8u (cycles, n=%u)\n", s->name,
			s->min, (uint32_t)(avg_x100 / 100), (uint32_t)(avg_x100 % 100), s->max,
			s->count);
		s->reported = true;
	}
}

#define BENCH_BEGIN() ArchMeasure()
#define BENCH_END(stat, start_val) bench_record(&(stat), ArchMeasure() - (start_val))

#endif /* ZUZU_BENCH */

#endif /* ZUZU_KERNEL_BENCH_H */
