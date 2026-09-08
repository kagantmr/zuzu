/* zone.h - zuzuOS notification-based zone library. */

#ifndef ZUZU_SYNC_ZONE
#define ZUZU_SYNC_ZONE

#include "zuzu/err.h"
#include <zuzu/types.h>
#include <stdatomic.h>

typedef struct zoneobj {
    Tid          owner;
    _Atomic int  locked;
    Handle       ntfn;
} Zone;

Err ZoneInit(Zone* z);

Err ZoneDestroy(Zone *z);

Err ZoneEnter(Zone *z);

Err ZoneExit(Zone *z);

Err ZoneTryEnter(Zone *z);

static inline void _ZoneCleanup(Zone **zp) {
    if (*zp) ZoneExit(*zp);
}

#define IN_ZONE(zptr) \
    for (Zone *_zone_guard __attribute__((cleanup(_ZoneCleanup))) = \
             (ZoneEnter(zptr), (zptr)), \
         *_zone_once = _zone_guard; \
         _zone_once; \
         _zone_once = NULL)

#endif /* ZUZU_SYNC_ZONE */
