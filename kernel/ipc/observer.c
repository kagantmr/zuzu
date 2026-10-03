#include "observer.h"

static void ObserverRelease(Observer *o)
{
    o->ev->bind_count--;
    EventDropReference(o->ev);
    o->ev = NULL;
    o->bit = 0;
}

void ObserverInit(ObserverSet *s)
{
    for (int i = 0; i < MAX_OBSERVERS; i++)
        s->slot[i] = (Observer){ .ev = NULL, .bit = 0 };
}

Err ObserverAdd(ObserverSet *s, EventObject *ev, uint32_t bit)
{
    for (int i = 0; i < MAX_OBSERVERS; i++)
    {
        if (s->slot[i].ev == ev)
        {
            s->slot[i].bit = (uint8_t)bit;
            ev->bound_mask |= (1U << bit);
            return ZUZU_OK;
        }
    }

    for (int i = 0; i < MAX_OBSERVERS; i++)
    {
        if (s->slot[i].ev && !s->slot[i].ev->alive)
            ObserverRelease(&s->slot[i]);
    }

    for (int i = 0; i < MAX_OBSERVERS; i++)
    {
        if (!s->slot[i].ev)
        {
            s->slot[i] = (Observer){ .ev = ev, .bit = (uint8_t)bit };
            ev->ref_count++;
            ev->bind_count++;
            ev->bound_mask |= (1U << bit);
            return ZUZU_OK;
        }
    }
    return ERR_NOMEM;
}

void ObserverNotify(ObserverSet *s)
{
    for (int i = 0; i < MAX_OBSERVERS; i++)
    {
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
    for (int i = 0; i < MAX_OBSERVERS; i++)
    {
        if (s->slot[i].ev)
            ObserverRelease(&s->slot[i]);
    }
}
