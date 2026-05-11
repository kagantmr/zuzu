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
    return sp->uptime_ms / 1000;
}

static inline void clock_gettime(struct timespec *ts) {
    syspage_t *sp = (syspage_t *)SYSPAGE;
    uint32_t ms = sp->uptime_ms;
    ts->tv_sec  = ms / 1000;
    ts->tv_nsec = (ms % 1000) * 1000000;
}

#ifdef __cplusplus
}
#endif

#endif /* TIME_H */