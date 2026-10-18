/**
 * @file sem.h
 * @brief Semaphore definitions.
 */

#ifndef ZUZU_SYNC_SEM_H
#define ZUZU_SYNC_SEM_H

#include "zuzu/err.h"
#include <stdatomic.h>
#include <types.h>

/**
 * @brief Struct representing a semaphore.
 */
typedef struct sem {
    _Atomic int count; /**< An atomic integer representing the semaphore's count. */
    Handle event; /**< Event associated with the semaphore. */
} Semaphore;

/**
 * @brief Initialize a semaphore to an initial count.
 * 
 * @param[in] s The semaphore to initialize.
 * @param[in] initial_count The count to initialize the semaphore to.
 * 
 * @return ZUZU_OK on success, negative error code if the event couldn't be created.
 */
Err SemInit(Semaphore *s, int initial_count);

/**
 * @brief Destroy a semaphore.
 * 
 * @param[in] s The semaphore to destroy.
 * 
 * @return ZUZU_OK on success, ERR_BUSY if the event cannot be destroyed.
 */
Err SemDestroy(Semaphore *s);

/**
 * @brief Increment a semaphore's count, unblocking a waiter if it was negative.
 * 
 * @param[in] s The semaphore to post.
 * 
 * @return ZUZU_OK on success, ERR_BUSY if the event cannot be destroyed.
 */
Err SemPost(Semaphore *s);

/**
 * @brief Decrement a semaphore's count, and then block until it's incremented.
 * 
 * @param[in] s The semaphore to wait on.
 * 
 * @return ZUZU_OK on success.
 */
Err SemWait(Semaphore *s);

#endif /* ZUZU_SYNC_SEM_H */
