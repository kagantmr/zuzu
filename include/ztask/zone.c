/**
 * @file zone.h
 * @brief Implementation for zone synchronization utilities.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <ztask/zone.h>
#include <types.h>
#include <util/tls.h>
#include <zuzu/zuzu.h>

Err ZoneInit(Zone *z)
{
    z->locked = 0;
    z->owner = 0;
    z->event = CreateEvent();
    if (z->event < 0) {
        return z->event;
    }
    return ZUZU_OK;
}

Err ZoneDestroy(Zone *z)
{
    if (HandleDestroy(z->event) != ZUZU_OK)
        return ERR_BUSY;
    z->locked = 0;
    z->owner = 0;
    return ZUZU_OK;
}

Err ZoneEnter(Zone *z)
{
    int c = 0;
    if (atomic_compare_exchange_strong(&z->locked, &c, 1))
        goto acquired;                          /* fast path, no syscall */

    if (c != 2)
        c = atomic_exchange(&z->locked, 2);     /* announce a waiter */
    while (c != 0) {
        WaitOn(z->event, TIMEOUT_INFINITE);
        c = atomic_exchange(&z->locked, 2);     /* take it, keep "waiters" set */
    }
acquired:
    z->owner = GetTls()->tid;
    return ZUZU_OK;
}

void ZoneExit(Zone *z)
{
    z->owner = 0;
    if (atomic_exchange(&z->locked, 0) == 2)    /* only signal if someone may wait */
        Signal(z->event, 1, false);
}

Err ZoneTryEnter(Zone *z)
{
    int c = 0;
    if (!atomic_compare_exchange_strong(&z->locked, &c, 1))
        return ERR_BUSY;    /* held (1 or 2): never block, never touch the waiters state */
    z->owner = GetTls()->tid;
    return ZUZU_OK;
}
