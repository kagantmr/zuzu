// arch/timer.h - Neutral periodic-timer contract.
//
//   void arch_timer_init(void);  - start the periodic tick that drives
//                                   the scheduler via the kernel tick subsystem.

#ifndef ZUZU_ARCH_TIMER_H
#define ZUZU_ARCH_TIMER_H

#include <stdint.h>
#include <zuzu/types.h>

void ArchTimerInit(void);

Time ArchTimerNow(void);
uint32_t ArchTimerFreq(void);
void ArchTimerSetDeadline(Time abs_count);
void ArchTimerDisable(void);

static inline uint64_t ArchDeadlineFromMs(uint32_t ms)
{
    uint64_t delta = ((uint64_t)ms * (uint64_t)ArchTimerFreq()) / 1000U;
    return (uint64_t)ArchTimerNow() + delta;
}

#endif // ZUZU_ARCH_TIMER_H
