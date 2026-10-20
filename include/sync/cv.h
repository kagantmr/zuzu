/**
 * @file cv.h
 * @brief Condition variable definitions.
 */

#ifndef ZUZU_SYNC_CV
#define ZUZU_SYNC_CV

#include "zuzu/err.h"
#include <stdatomic.h>
#include <stdint.h>
#include <sync/sem.h>
#include <sync/zone.h>
#include <types.h>

/**
 * @brief Struct representing a condition variable.
 */
typedef struct {
    _Atomic uint32_t seq; /**< Wakeup generation. Bumped by every signal and broadcast. */
    _Atomic int waiters;  /**< The number of threads currently inside CondVarWait(). */
    Semaphore sem;        /**< The semaphore waiting threads block on. */
} CondVariable;

/**
 * @brief Initialize a condition variable with no waiters.
 *
 * @param[in] cv The condition variable to initialize.
 * @return ZUZU_OK on success, negative error code if the underlying semaphore couldn't be created.
 */
Err CondVarInit(CondVariable *cv);

/**
 * @brief Destroy a condition variable.
 *
 * No thread may be waiting on it.
 *
 * @param[in] cv The condition variable to destroy.
 * @return ZUZU_OK on success, ERR_BUSY if the underlying semaphore cannot be destroyed.
 */
Err CondVarDestroy(CondVariable *cv);

/**
 * @brief Wake one thread waiting on a condition variable.
 *
 * Does nothing if no thread is waiting. The caller normally holds the zone that protects the
 * condition, but doesn't have to.
 *
 * @param[in] cv The condition variable to signal.
 * @return ZUZU_OK.
 */
Err CondVarSignal(CondVariable *cv);

/**
 * @brief Wake every thread waiting on a condition variable.
 *
 * @param[in] cv The condition variable to signal.
 * @return ZUZU_OK.
 */
Err CondVarBroadcast(CondVariable *cv);

/**
 * @brief Release a zone, wait to be signaled, and take the zone again.
 *
 * The caller must hold @p z. It is released before blocking and held again when this returns, so
 * the condition must be re-checked in a loop: a wakeup does not guarantee it still holds.
 *
 * @param[in] cv The condition variable to wait on.
 * @param[in] z The zone that protects the condition; held on entry and on return.
 * @return ZUZU_OK.
 */
Err CondVarWait(CondVariable *cv, Zone *z);

#endif /* ZUZU_SYNC_CV */
