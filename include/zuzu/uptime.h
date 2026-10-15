#ifndef ZUZU_UPTIME_H
#define ZUZU_UPTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/counter.h>
#include <stdint.h>
#include <zuzu/syspage.h>

/**
 * @brief Converts a counter delta to units of 1/per_sec seconds without overflowing
 * on long uptimes.
 */
static inline uint64_t UptimeCountsTo(uint64_t counts, uint64_t per_sec)
{
    uint64_t f = ((const volatile Syspage *)SYSPAGE)->counter_freq;
    if (!f)
        return 0;
    return (counts / f) * per_sec + ((counts % f) * per_sec) / f;
}

/** @brief Counter ticks since boot, at counter_freq Hz. */
static inline uint64_t UptimeCounts(void)
{
    return ArchCounterRead() - ((const volatile Syspage *)SYSPAGE)->counter_boot;
}

/** @brief Milliseconds since boot. */
static inline uint64_t UptimeMs(void) { return UptimeCountsTo(UptimeCounts(), 1000u); }

/** @brief Microseconds since boot. */
static inline uint64_t UptimeUs(void) { return UptimeCountsTo(UptimeCounts(), 1000000u); }

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_UPTIME_H */
