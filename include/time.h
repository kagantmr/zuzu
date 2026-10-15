#ifndef TIME_H
#define TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/syspage.h>
#include <zuzu/uptime.h>

typedef uint64_t Time;

typedef struct timespec {
    Time tv_sec;
    long tv_nsec;
} Timespec;

struct tm {
    int tm_sec;   /* seconds after the minute - [0, 60] including leap second */
    int tm_min;   /* minutes after the hour - [0, 59] */
    int tm_hour;  /* hours since midnight - [0, 23] */
    int tm_mday;  /* day of the month - [1, 31] */
    int tm_mon;   /* months since January - [0, 11] */
    int tm_year;  /* years since 1900 */
    int tm_wday;  /* days since Sunday - [0, 6] */
    int tm_yday;  /* days since January 1 - [0, 365] */
    int tm_isdst; /* daylight savings time flag */
};

/**
 * @brief Fills ts with the time since boot. zuzu keeps no wall clock; the epoch is boot.
 *
 * @param ts Pointer to a timespec structure that will be filled with the uptime.
 */
static inline void ClockGetTime(struct timespec *ts)
{
    uint64_t counts = UptimeCounts();
    uint64_t f = ((const volatile Syspage *)SYSPAGE)->counter_freq;
    if (!f) {
        ts->tv_sec = 0;
        ts->tv_nsec = 0;
        return;
    }
    ts->tv_sec = counts / f;
    ts->tv_nsec = (long)(((counts % f) * 1000000000ULL) / f);
}

#ifdef __cplusplus
}
#endif

#endif /* TIME_H */
