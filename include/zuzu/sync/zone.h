/**
 * @file zone.h
 * @brief Notification-based zones (zuzuOS user-space mutex).
 *
 * A zone provides mutual exclusion between threads of a process. Uncontended
 * enter/exit is a single atomic op; contended waiters block on a kernel
 * notification object rather than spinning.
 */

#ifndef ZUZU_SYNC_ZONE
#define ZUZU_SYNC_ZONE

#include "zuzu/err.h"
#include <zuzu/types.h>
#include <stdatomic.h>

/**
 * @defgroup sync_zone Zone
 * @ingroup sync
 * @brief Mutual exclusion for zuzu user-space threads.
 * @{
 */

/**
 * @brief An initialised zone.
 *
 * Treat as opaque since layout may change. Zero-initialisation is *not* a valid
 * unlocked state, always call @ref ZoneInit.
 */
typedef struct {
    Tid         owner;   ///< Owning thread while locked; undefined when unlocked.
    _Atomic int locked;  ///< 0 = free, 1 = held. Contended waiters spin-check then block.
    Handle      ntfn;    ///< Kernel notification object waiters block on.
} Zone;

/**
 * @brief Initialise a zone to the unlocked state.
 * @param z  Storage for the zone. Contents are overwritten.
 * @return @c ERR_OK on success.
 * @retval ERR_INVAL  @p z is NULL.
 * @retval ERR_NOMEM  Could not allocate the backing notification object.
 */
Err ZoneInit(Zone *z);

/**
 * @brief Release a zone and its backing notification object.
 *
 * The zone must be unlocked and have no waiters. Using @p z afterwards is
 * UB.
 *
 * @param z  Zone to destroy.
 * @return @c ERR_OK on success.
 * @retval ERR_INVAL  @p z is NULL or uninitialised.
 * @retval ERR_BUSY   The zone is held or has waiters.
 */
Err ZoneDestroy(Zone *z);

/**
 * @brief Acquire the zone, blocking until it is free.
 * @param z  Initialised zone.
 * @return @c ERR_OK once the zone is held by the caller.
 * @retval ERR_INVAL  @p z is NULL or uninitialised.
 * @retval ERR_DEAD   Backing notification object was destroyed.
 * @note Blocking. Recursive entry by the owner is undefined behaviour.
 * @see ZoneExit, ZoneTryEnter
 */
Err ZoneEnter(Zone *z);

/**
 * @brief Release the zone, waking one waiter if any.
 * @param z  Zone currently held by the caller.
 * @return @c ERR_OK on success.
 * @retval ERR_INVAL  @p z is NULL or uninitialised.
 * @retval ERR_PERM   The calling thread is not the owner.
 * @see ZoneEnter
 */
Err ZoneExit(Zone *z);

/**
 * @brief Try to acquire the zone without blocking.
 * @param z  Initialised zone.
 * @return @c ERR_OK if the zone was acquired.
 * @retval ERR_BUSY   The zone is currently held.
 * @retval ERR_INVAL  @p z is NULL or uninitialised.
 * @see ZoneEnter
 */
Err ZoneTryEnter(Zone *z);

/// Release callback for the @ref IN_ZONE cleanup attribute. @private
static inline void _ZoneCleanup(Zone **zp) {
    if (*zp) ZoneExit(*zp);
}

/**
 * @brief Scoped zone guard: acquire now, release when the block exits.
 *
 * @param zptr  Pointer to an initialised @ref Zone.
 *
 * @code
 * IN_ZONE(&z) {
 *     // critical section; zone released on any exit from this block
 * }
 * @endcode
 *
 * @warning Do not @c longjmp or thread-exit out of the block.
 */
#define IN_ZONE(zptr) \
    for (Zone *_zone_guard __attribute__((cleanup(_ZoneCleanup))) = \
             (ZoneEnter(zptr), (zptr)), \
         *_zone_once = _zone_guard; \
         _zone_once; \
         _zone_once = NULL)

/** @} */ // sync_zone

#endif /* ZUZU_SYNC_ZONE */
