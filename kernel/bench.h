/**
 * @file bench.h
 * @brief Cycle-count micro-benchmark helpers, compiled only when CONFIG_ZUZU_BENCH is set.
 */

#ifndef KERNEL_BENCH_H
#define KERNEL_BENCH_H

#ifdef CONFIG_ZUZU_BENCH

#include <arch/cycles.h>
#include <stdbool.h>
#include <types.h>

/**
 * @brief Running statistics for one benchmarked code path, in cycles.
 */
typedef struct BenchStat {
    const char *name; /**< Label printed when the statistics are dumped. */
    uint32_t min; /**< Smallest sample recorded since the last reset. */
    uint32_t max; /**< Largest sample recorded since the last reset. */
    uint64_t sum; /**< Sum of all samples, used to compute the average. */
    uint32_t count; /**< Number of samples recorded. */
    bool linked; /**< The statistic has been added to the global list. */
    struct BenchStat *next; /**< Next statistic in the global list. */
} BenchStat;

/**
 * @brief Add a statistic to the global list that BenchDumpAll() and BenchResetAll() walk.
 *
 * Called automatically by bench_record() on the first sample.
 *
 * @param[in,out] s The statistic to link; its @c linked flag is set.
 */
void BenchLink(BenchStat *s);
/**
 * @brief Print the minimum, average and maximum of every linked statistic that has samples.
 */
void BenchDumpAll(void);
/**
 * @brief Clear the samples of every linked statistic.
 */
void BenchResetAll(void);

/**
 * @brief Define a file-local BenchStat.
 *
 * @param varname Name of the variable to define.
 * @param label String printed for the statistic when dumped.
 */
#define BENCH_STAT(varname, label) static BenchStat varname = {.name = (label)}

/**
 * @brief Add one sample to a statistic, linking it on first use.
 *
 * @param[in,out] s The statistic to update.
 * @param[in] cycles The measured duration in cycles.
 */
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

/**
 * @brief Take the starting cycle count of a measurement.
 */
#define BENCH_BEGIN() ArchMeasure()
/**
 * @brief Record the cycles elapsed since a BENCH_BEGIN() reading into a statistic.
 *
 * @param stat The BenchStat variable to update (not a pointer).
 * @param start_val The value returned by BENCH_BEGIN().
 */
#define BENCH_END(stat, start_val) bench_record(&(stat), ArchMeasure() - (start_val))

#endif /* CONFIG_ZUZU_BENCH */

#endif /* KERNEL_BENCH_H */
