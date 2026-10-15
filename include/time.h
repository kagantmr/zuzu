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
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

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
