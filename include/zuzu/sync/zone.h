/* zone.h - zuzuOS notification-based zone library. */

#ifndef ZUZU_SYNC_ZONE
#define ZUZU_SYNC_ZONE

#include "zuzu/err.h"
#include <zuzu/types.h>

typedef struct zoneobj Zone;

Err ZoneInit(Zone* z);

Err ZoneDestroy(Zone *z);

Err ZoneEnter(Zone *z);

Err ZoneExit(Zone *z);

Err ZoneTryEnter(Zone *z);

static inline void _ZoneCleanup(Zone **zp) {
    ZoneExit(*zp);
}

#define IN_ZONE(zptr) \
    for (Zone *_zone_guard __attribute__((cleanup(_ZoneCleanup))) = \
             (ZoneEnter(zptr), (zptr)); \
         _zone_guard; \
         _zone_guard = NULL)

#endif /* ZUZU_SYNC_ZONE */