#ifndef NETD_TIMER_H
#define NETD_TIMER_H

#include <list.h>
#include <stdbool.h>
#include <types.h>

#define TIMER_NO_DEADLINE UINT32_MAX

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
uint32_t TimerNextDeadline(void);
void TimerRunExpired(void);

#endif
