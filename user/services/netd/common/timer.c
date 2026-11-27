#include "timer.h"
#include "globals.h"

_Static_assert((TIMER_WHEEL_SLOTS & (TIMER_WHEEL_SLOTS - 1)) == 0,
               "TIMER_WHEEL_SLOTS must be a power of two");

static ListHead wheel[TIMER_WHEEL_SLOTS];
static uint32_t done_tick;
static uint32_t armed_count;

static inline uint32_t TickOf(uint32_t ms) { return ms / TIMER_TICK_MS; }

static inline ListHead *SlotOf(uint32_t tick) { return &wheel[tick & (TIMER_WHEEL_SLOTS - 1)]; }

void TimerInit(void)
{
    for (uint32_t i = 0; i < TIMER_WHEEL_SLOTS; i++)
        ListInit(&wheel[i]);
    done_tick = TickOf(NetNowMs()) - 1;
    armed_count = 0;
}

void TimerCancel(Timer *t)
{
    if (!t->armed)
        return;
    ListRemove(&t->node);
    t->armed = false;
    armed_count--;
}

void TimerArm(Timer *t, uint32_t deadline_ms, void (*cb)(void *arg), void *arg)
{
    TimerCancel(t);
    t->deadline_ms = deadline_ms;
    t->cb = cb;
    t->arg = arg;

    uint32_t tick = TickOf(deadline_ms);
    if ((int32_t)(tick - (done_tick + 1)) < 0)
        tick = done_tick + 1;
    ListAddTail(&t->node, &SlotOf(tick)->node);
    t->armed = true;
    armed_count++;
}

uint32_t TimerNextDeadline(uint32_t within_ms)
{
    if (armed_count == 0)
        return TIMER_NO_DEADLINE;

    uint32_t first = done_tick + 1;
    uint32_t slots = TickOf(within_ms) + 2;
    if (slots > TIMER_WHEEL_SLOTS)
        slots = TIMER_WHEEL_SLOTS;
    for (uint32_t i = 0; i < slots; i++) {
        uint32_t tick = first + i;
        uint32_t end_ms = (tick + 1) * TIMER_TICK_MS;
        bool found = false;
        uint32_t best = 0;
        ListNode *pos;
        LIST_FOR_EACH(pos, &SlotOf(tick)->node)
        {
            uint32_t d = container_of(pos, Timer, node)->deadline_ms;
            if ((int32_t)(d - end_ms) >= 0)
                continue;
            if (!found || (int32_t)(d - best) < 0)
                best = d;
            found = true;
        }
        if (found)
            return best;
    }
    return TIMER_NO_DEADLINE;
}

void TimerRunExpired(void)
{
    uint32_t now = NetNowMs();
    uint32_t now_tick = TickOf(now);
    uint32_t span = now_tick - done_tick;
    if ((int32_t)span <= 0)
        return;
    if (span > TIMER_WHEEL_SLOTS)
        span = TIMER_WHEEL_SLOTS;

    ListHead expired;
    ListInit(&expired);
    for (uint32_t i = 0; i < span; i++) {
        ListHead *slot = SlotOf(now_tick - i);
        ListNode *pos, *n;
        LIST_FOR_EACH_SAFE(pos, n, &slot->node)
        {
            Timer *t = container_of(pos, Timer, node);
            if ((int32_t)(now - t->deadline_ms) < 0)
                continue;
            ListRemove(&t->node);
            ListAddTail(&t->node, &expired.node);
        }
    }
    done_tick = now_tick - 1;

    ListNode *node;
    while ((node = ListPopFront(&expired)) != NULL) {
        Timer *t = container_of(node, Timer, node);
        t->armed = false;
        armed_count--;
        t->cb(t->arg);
    }
}
