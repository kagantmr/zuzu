#include "timer.h"
#include "globals.h"
#include <mem.h>

static timer_t timers[TIMER_MAX];

void timer_init(void) {
    memset(timers, 0, sizeof(timer_t) * TIMER_MAX);
}

timer_handle_t timer_arm(uint32_t deadline_ms, void (*cb)(void *arg), void *arg)
{
    for (int i = 0; i < TIMER_MAX; i++)
    {
        if (!timers[i].active)
        {
            timers[i].active = true;
            timers[i].cb = cb;
            timers[i].deadline_ms = deadline_ms;
            timers[i].arg = arg;
            return i;
        }
    }
    return TIMER_NONE;
}

void timer_cancel(timer_handle_t h)
{
    if (h < 0 || h >= TIMER_MAX)
        return;                     /* TIMER_NONE and garbage: harmless no-op */
    timers[h].active = false;
}
uint32_t timer_next_deadline(void)
{
    uint32_t earliest = TIMER_NO_DEADLINE;
    for (int i = 0; i < TIMER_MAX; i++)
        if (timers[i].active && timers[i].deadline_ms < earliest)
            earliest = timers[i].deadline_ms;
    return earliest;
}

void timer_run_expired(void)
{
    uint32_t now = net_now_ms();
    for (int i = 0; i < TIMER_MAX; i++)
    {
        if (timers[i].active && (int32_t)(now - timers[i].deadline_ms) >= 0)
        {
            timers[i].active = false;       /* deactivate BEFORE the callback */
            timers[i].cb(timers[i].arg);
        }
    }
}
