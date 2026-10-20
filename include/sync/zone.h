/**
 * @file zone.h
 * @brief Critical zone synchronization utilities.
 */

#ifndef ZUZU_SYNC_ZONE_H
#define ZUZU_SYNC_ZONE_H

#include "zuzu/err.h"
#include <stdatomic.h>
#include <types.h>

/**
 * @brief Struct describing a zone.
 */
typedef struct {
    Tid owner; /**< Current owner. -1 means no owner. */
    _Atomic int locked; /**< Atomic integer showing the zone is entered. */
    Handle event; /**< On contention, the event to use. */
} Zone;

/**
 * @brief Create a Zone.
 * 
 * @param[in] z Pointer to a zone object to initialize.
 * @return ZUZU_OK on success, negative error code if the Event object couldn't be created.
 */
Err ZoneInit(Zone *z);

/**
 * @brief Destroy a Zone.
 * 
 * @param[in] z Pointer to a zone object to destroy.
 * @return ZUZU_OK on success, negative error code if the Event object couldn't be destroyed.
 */
Err ZoneDestroy(Zone *z);

/**
 * @brief Try entering a Zone and block if it is occupied.
 * 
 * @param[in] z The zone to enter to.
 * @return ZUZU_OK on success.
 */
Err ZoneEnter(Zone *z);

/**
 * @brief Exit a Zone.
 * 
 * @param[in] z The zone to exit from.
 */
void ZoneExit(Zone *z);

/**
 * @brief Try entering a Zone and return with an error if occupied.
 * 
 * @param[in] z The zone to enter to.
 * @return ZUZU_OK on success, ERR_BUSY if the zone is occupied.
 */
Err ZoneTryEnter(Zone *z);

/** 
 * @brief Helper for IN_ZONE macro.
 * 
 * @param[in] zp Pointer to a pointer to a zone.
 */
static inline void _ZoneCleanup(Zone **zp)
{
    if (*zp)
        ZoneExit(*zp);
}

/**
 * @brief Zone enter and exit wrapper.
 */
#define IN_ZONE(zptr)                                                                              \
    for (Zone * _zone_guard __attribute__((cleanup(_ZoneCleanup))) = (ZoneEnter(zptr), (zptr)),    \
                            *_zone_once = _zone_guard;                                             \
         _zone_once; _zone_once = NULL)

#endif /* ZUZU_SYNC_ZONE_H */
