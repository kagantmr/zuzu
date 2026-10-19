/**
 * @file clock.h
 * @brief Time calculation utilities.
 */

#ifndef KERNEL_CLOCK_H
#define KERNEL_CLOCK_H

#include <arch/timer.h>
#include <stdint.h>

/**
 * @brief Convert a count of timer ticks into a unit of time.
 *
 * Divides in two steps, whole seconds and the remainder, so the multiplication cannot overflow
 * 64 bits.
 *
 * @param[in] counts The number of timer ticks.
 * @param[in] per_sec Units per second to convert to, for example 1000 for milliseconds.
 * @return The converted value, rounded down, or 0 if the timer frequency is not yet known.
 */
static inline uint64_t ClockCountsTo(uint64_t counts, uint64_t per_sec)
{
    uint64_t f = ArchTimerFreq();
    if (!f)
        return 0;
    return ((counts / f) * per_sec) + (((counts % f) * per_sec) / f);
}

/**
 * @brief Get the timer's current time in milliseconds since it started counting.
 *
 * @return The time, rounded down.
 */
static inline uint64_t ClockNowMs(void) { return ClockCountsTo(ArchTimerNow(), 1000); }
/**
 * @brief Get the timer's current time in microseconds since it started counting.
 *
 * @return The time, rounded down.
 */
static inline uint64_t ClockNowUs(void) { return ClockCountsTo(ArchTimerNow(), 1000000); }

/**
 * @brief Convert microseconds into timer ticks.
 *
 * @param[in] us The duration in microseconds.
 * @return The number of timer ticks, rounded down.
 */
static inline uint64_t ClockUsToCounts(uint64_t us)
{
    return (us * ArchTimerFreq()) / 1000000U;
}

#endif /* KERNEL_CLOCK_H */
