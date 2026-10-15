#ifndef KERNEL_TIME_CLOCK_H
#define KERNEL_TIME_CLOCK_H

#include <arch/timer.h>
#include <stdint.h>

static inline uint64_t ClockCountsTo(uint64_t counts, uint64_t per_sec)
{
    uint64_t f = ArchTimerFreq();
    if (!f)
        return 0;
    return ((counts / f) * per_sec) + (((counts % f) * per_sec) / f);
}

static inline uint64_t ClockNowMs(void) { return ClockCountsTo(ArchTimerNow(), 1000); }
static inline uint64_t ClockNowUs(void) { return ClockCountsTo(ArchTimerNow(), 1000000); }

static inline uint64_t ClockUsToCounts(uint64_t us)
{
    return (us * ArchTimerFreq()) / 1000000U;
}

#endif
