/**
 * @file sem.h
 * @brief Semaphore implementation.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <sync/sem.h>
#include <util/tls.h>
#include <zuzu/zuzu.h>

Err SemInit(Semaphore *s, int initial_count)
{
    s->count = initial_count;
    s->event = CreateEvent();
    if (s->event < 0) {
        return s->event;
    }
    return ZUZU_OK;
}

Err SemDestroy(Semaphore *s)
{
    if (HandleDestroy(s->event) != ZUZU_OK)
        return ERR_BUSY;
    s->count = 0;
    return ZUZU_OK;
}

Err SemPost(Semaphore *s)
{
    atomic_fetch_add(&s->count, 1);
    Signal(s->event, 1, false);
    return ZUZU_OK;
}

Err SemWait(Semaphore *s)
{
    while (1) {
        int expected = atomic_load(&s->count);
        while (expected > 0) {
            if (atomic_compare_exchange_weak(&s->count, &expected, expected - 1)) {
                return ZUZU_OK;
            }
        }
        WaitOn(s->event, TIMEOUT_INFINITE);
    }
}
