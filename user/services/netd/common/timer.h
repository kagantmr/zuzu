#ifndef NETD_TIMER_H
#define NETD_TIMER_H

#include <list.h>
#include <stdbool.h>
#include <types.h>

#define TIMER_NO_DEADLINE UINT32_MAX

/** @brief Width of one wheel slot in milliseconds. */
#define TIMER_TICK_MS 8u
/** @brief Number of wheel slots; a power of two. One turn covers TIMER_TICK_MS * TIMER_WHEEL_SLOTS. */
#define TIMER_WHEEL_SLOTS 256u

typedef struct {
    ListNode node;
    uint32_t deadline_ms; /* absolute, vs NetNowMs() */
    void (*cb)(void *arg);
    void *arg;
    bool armed;
} Timer;

void TimerInit(void);
void TimerArm(Timer *t, uint32_t deadline_ms, void (*cb)(void *arg), void *arg);
void TimerCancel(Timer *t);
/** @brief Earliest deadline due within @p within_ms from now, or TIMER_NO_DEADLINE. */
uint32_t TimerNextDeadline(uint32_t within_ms);
void TimerRunExpired(void);

#endif
