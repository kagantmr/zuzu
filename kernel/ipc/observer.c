/**
 * @file observer.c
 * @brief Observer functions implementation
 */

#include "observer.h"

/**
 * @brief Release an observer's event.
 *
 * @param[in] o The observer to release.
 */
static void ObserverRelease(Observer *o)
{
    EventUnbindBit(o->ev, o->bit);
    if (o->from_event && o->ev->child_count)
        o->ev->child_count--;
    EventUnref(o->ev);
    o->ev = NULL;
    o->bit = 0;
    o->from_event = false;
}

void ObserverInit(ObserverSet *s)
{
    for (int i = 0; i < MAX_OBSERVERS; i++)
        s->slot[i] = (Observer){.ev = NULL, .bit = 0, .from_event = false};
}

Err ObserverAdd(ObserverSet *s, EventObject *ev, uint32_t bit, bool is_event)
{
    for (int i = 0; i < MAX_OBSERVERS; i++) {
        if (s->slot[i].ev == ev) {
            if (s->slot[i].bit != bit) {
                EventUnbindBit(ev, s->slot[i].bit);
                EventBindBit(ev, bit);
                s->slot[i].bit = (uint8_t)bit;
            }
            return ZUZU_OK;
        }
    }

    for (int i = 0; i < MAX_OBSERVERS; i++) {
        if (s->slot[i].ev && !s->slot[i].ev->alive)
            ObserverRelease(&s->slot[i]);
    }

    for (int i = 0; i < MAX_OBSERVERS; i++) {
        if (!s->slot[i].ev) {
            s->slot[i] = (Observer){.ev = ev, .bit = (uint8_t)bit, .from_event = is_event};
            ev->ref_count++;
            if (is_event)
                ev->child_count++;
            EventBindBit(ev, bit);
            return ZUZU_OK;
        }
    }
    return ERR_NOMEM;
}

bool ObserverEmpty(const ObserverSet *s)
{
    for (int i = 0; i < MAX_OBSERVERS; i++) {
        if (s->slot[i].ev && s->slot[i].ev->alive)
            return false;
    }
    return true;
}

void ObserverNotify(ObserverSet *s)
{
    for (int i = 0; i < MAX_OBSERVERS; i++) {
        Observer *o = &s->slot[i];
        if (!o->ev)
            continue;
        if (!o->ev->alive)
            ObserverRelease(o);
        else
            EventSignal(o->ev, (1U << o->bit), false);
    }
}

void ObserverClear(ObserverSet *s)
{
    for (int i = 0; i < MAX_OBSERVERS; i++) {
        if (s->slot[i].ev)
            ObserverRelease(&s->slot[i]);
    }
}
