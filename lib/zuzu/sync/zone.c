#include "zuzu/cap.h"
#include "zuzu/err.h"
#include "zuzu/types.h"
#include <zuzu/sync/zone.h>
#include <zuzu/ntfn.h>
#include <stdbool.h>
#include <zuzu/tls.h>
#include <stdatomic.h>

typedef struct zoneobj {
    Tid owner;
    _Atomic int locked;    
    Handle ntfn;
} Zone;

Err ZoneInit(Zone* z) {
    z->locked = 0;
    z->owner = 0;
    z->ntfn = ZuzuNtfnCreate();
    if (z->ntfn < 0) {
        return z->ntfn;
    }
    return ZUZU_OK;
}

Err ZoneDestroy(Zone *z) {
    if (ZuzuDestroy(z->ntfn) != ZUZU_OK) return ERR_BUSY;
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
        ZuzuNtfnWait(z->ntfn, TIMEOUT_INFINITE);
    }
    return ERR_DEAD;
}

Err ZoneExit(Zone *z) {
    z->owner = 0;
    atomic_store(&z->locked, 0);
    ZuzuNtfnSignal(z->ntfn, 1);
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
