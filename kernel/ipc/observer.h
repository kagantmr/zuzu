/**
 * @file observer.h
 * @brief An observer is a kernel-kept registry that signals an event bit if a state change has
 * occurred. This allows multiplexing of various sources of signals onto one event: ports, tasks,
 * spaces... state changes on all of them can appear as a bit in an event.
 */

#ifndef KERNEL_IPC_OBSERVER_H
#define KERNEL_IPC_OBSERVER_H

#include "event.h"
#include <stdint.h>
#include <zuzu/err.h>

/**
 * @brief Limit of observers on an object.
 */
#define MAX_OBSERVERS 4

/**
 * @brief Represents an observer with its event and which bit it is bound to.
 */
typedef struct {
    EventObject *ev; /**< The bound event object. */
    uint8_t bit;     /**< The bit position it will signal. */
} Observer;

/**
 * @brief Set of observers for objects.
 */
typedef struct {
    Observer slot[MAX_OBSERVERS]; /**< List of observers. */
} ObserverSet;

/**
 * @brief Initialize a set of observers
 * 
 * @param[in] s The set of observers.
 */
void ObserverInit(ObserverSet *s);

/**
 * @brief Add an observer to a observer set.
 * 
 * @param[in] s The set of observers.
 * @param[in] ev The event that will be signalled.
 * @param[in] bit The bit position that will be signalled.
 * 
 * @return Err
 * @retval ERR_NOMEM if the observer set is full.
 */
Err ObserverAdd(ObserverSet *s, EventObject *ev, uint32_t bit);

/**
 * @brief Signal all observers in a set that change has occurred.
 * 
 * @param[in] s The set of observers.
 */
void ObserverNotify(ObserverSet *s);

/**
 * @brief Empty all observers from a set.
 * 
 * @param[in] s The set of observers.
 */
void ObserverClear(ObserverSet *s);

#endif /* KERNEL_IPC_OBSERVER_H */
