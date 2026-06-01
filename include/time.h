#ifndef TIME_H
#define TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zuzu/syspage.h>

typedef uint64_t ztime_t;

typedef struct timespec {
    ztime_t tv_sec;
    long   tv_nsec;
} timespec_t;

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

static inline ztime_t time_now(void) {
    syspage_t *sp = (syspage_t *)SYSPAGE;
    return sp->boot_time_s + (sp->uptime_ticks / sp->tick_hz);
}

static inline void clock_gettime(struct timespec *ts) {
    syspage_t *sp = (syspage_t *)SYSPAGE;
    ztime_t ticks = sp->uptime_ticks;
    uint32_t hz = sp->tick_hz;
    ts->tv_sec  = ticks / hz;
    ts->tv_nsec = ((ticks % hz) * 1000000000ULL) / hz;
}

#ifdef __cplusplus
}
#endif

#endif /* TIME_H */
