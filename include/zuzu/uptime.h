/**
 * @file uptime.h
 * @brief Uptime calculation utilities.
 */

#ifndef ZUZU_UPTIME_H
#define ZUZU_UPTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/counter.h>
#include <stdint.h>
#include <zuzu/syspage.h>

/**
 * @brief Convert a count of hardware counter ticks into a unit of time.
 *
 * Splits the division into whole seconds and a remainder so the multiplication cannot overflow
 * 64 bits for any realistic uptime.
 *
 * @param[in] counts The number of counter ticks, such as the result of UptimeCounts().
 * @param[in] per_sec Units per second to convert to, for example 1000 for milliseconds.
 * @return The converted value, rounded down, or 0 if the counter frequency is not yet known.
 */
static inline uint64_t UptimeCountsTo(uint64_t counts, uint64_t per_sec)
{
    uint64_t f = ((const volatile Syspage *)SYSPAGE)->counter_freq;
    if (!f)
        return 0;
    return ((counts / f) * per_sec) + (((counts % f) * per_sec) / f);
}

/**
 * @brief Get the number of counter ticks since boot.
 *
 * @return The counter value now minus the value the kernel recorded at boot.
 */
static inline uint64_t UptimeCounts(void)
{
    return ArchCounterRead() - ((const volatile Syspage *)SYSPAGE)->counter_boot;
}

/**
 * @brief Get the time since boot in milliseconds.
 *
 * @return The uptime, rounded down.
 */
static inline uint64_t UptimeMs(void) { return UptimeCountsTo(UptimeCounts(), 1000U); }

/**
 * @brief Get the time since boot in microseconds.
 *
 * @return The uptime, rounded down.
 */
static inline uint64_t UptimeUs(void) { return UptimeCountsTo(UptimeCounts(), 1000000U); }

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_UPTIME_H */
