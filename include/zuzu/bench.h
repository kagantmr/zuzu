// zuzu/bench.h - shared running min/avg/max accumulator for userspace
// ZUZU_BENCH instrumentation (see kernel/bench.h for the kernel-side
// counterpart). Callers own printing: user/test_apps/speedtest uses printf,
// user/drivers/pl011drv can't (it *is* the console), so there's no
// print-on-completion baked in here.

#ifndef ZUZU_BENCH_H
#define ZUZU_BENCH_H

#ifdef ZUZU_BENCH

#include <arch/cycles.h>
#include <zuzu/types.h>

#define ZUZU_BENCH_WARMUP_ITERS 500u
#define ZUZU_BENCH_ITERS        100000u

typedef struct {
	uint32_t min;
	uint32_t max;
	uint64_t sum;
	uint32_t count;
} BenchResult;

static inline void bench_result_record(BenchResult *r, uint32_t cycles)
{
	if (r->count == 0 || cycles < r->min)
		r->min = cycles;
	if (cycles > r->max)
		r->max = cycles;
	r->sum += cycles;
	r->count++;
}

#endif /* ZUZU_BENCH */

#endif /* ZUZU_BENCH_H */
