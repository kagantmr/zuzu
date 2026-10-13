#include "timer.h"
#include "globals.h"

static ListHead timers;

void TimerInit(void) { ListInit(&timers); }

void TimerCancel(Timer *t)
{
    if (!t->armed)
        return;
    ListRemove(&t->node);
    t->armed = false;
}

void TimerArm(Timer *t, uint32_t deadline_ms, void (*cb)(void *arg), void *arg)
{
    TimerCancel(t);
    t->deadline_ms = deadline_ms;
    t->cb = cb;
    t->arg = arg;

    ListNode *pos;
    LIST_FOR_EACH(pos, &timers.node)
    {
        Timer *other = container_of(pos, Timer, node);
        if ((int32_t)(other->deadline_ms - deadline_ms) > 0)
            break;
    }
    ListAddTail(&t->node, pos);
    t->armed = true;
}

uint32_t TimerNextDeadline(void)
{
    if (timers.node.next == &timers.node)
        return TIMER_NO_DEADLINE;
    return container_of(timers.node.next, Timer, node)->deadline_ms;
}

void TimerRunExpired(void)
{
    uint32_t now = NetNowMs();
    while (timers.node.next != &timers.node) {
        Timer *t = container_of(timers.node.next, Timer, node);
        if ((int32_t)(now - t->deadline_ms) < 0)
            break;
        ListRemove(&t->node);
        t->armed = false;
        t->cb(t->arg);
    }
}
