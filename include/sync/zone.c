#include <types.h>
#include <sync/zone.h>
#include <zuzu/zuzu.h>
#include <stdbool.h>
#include <util/tls.h>
#include <stdatomic.h>

Err ZoneInit(Zone* z) {
    z->locked = 0;
    z->owner = 0;
    z->event = CreateEvent();
    if (z->event < 0) {
        return z->event;
    }
    return ZUZU_OK;
}

Err ZoneDestroy(Zone *z) {
    if (HandleDestroy(z->event) != ZUZU_OK) return ERR_BUSY;
    z->locked = 0;
    z->owner = 0;
    return ZUZU_OK;
}

Err ZoneEnter(Zone *z) {
    while (1) {
        int expected = 0;
        if (atomic_compare_exchange_weak(&z->locked, &expected, 1)) {
            z->owner = ZuzuTLS()->tid;
            return ZUZU_OK;
        }
        WaitOn(z->event, TIMEOUT_INFINITE);
    }
    return ERR_DEAD;
}

Err ZoneExit(Zone *z) {
    z->owner = 0;
    atomic_store(&z->locked, 0);
    Signal(z->event, 1, false);
    return ZUZU_OK;
}

Err ZoneTryEnter(Zone *z) {
    int expected = 0;
    if (atomic_compare_exchange_strong(&z->locked, &expected, 1)) {
        z->owner = ZuzuTLS()->tid;
        return ZUZU_OK;
    }
    return ERR_BUSY;
}
