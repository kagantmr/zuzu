#ifndef ZUZU_SYNC_ZONE
#define ZUZU_SYNC_ZONE

#include "zuzu/err.h"
#include <stdatomic.h>
#include <types.h>

typedef struct {
    Tid owner;
    _Atomic int locked;
    Handle event;
} Zone;

Err ZoneInit(Zone *z);

Err ZoneDestroy(Zone *z);

Err ZoneEnter(Zone *z);

Err ZoneExit(Zone *z);

Err ZoneTryEnter(Zone *z);

static inline void _ZoneCleanup(Zone **zp)
{
    if (*zp)
        ZoneExit(*zp);
}

#define IN_ZONE(zptr)                                                                              \
    for (Zone * _zone_guard __attribute__((cleanup(_ZoneCleanup))) = (ZoneEnter(zptr), (zptr)),    \
                            *_zone_once = _zone_guard;                                             \
         _zone_once; _zone_once = NULL)

#endif /* ZUZU_SYNC_ZONE */
