/**
 * @file observer.h
 * @brief An observer is a kernel-kept registry that signals an event bit if a state change has
 * occurred. This allows multiplexing of various sources of signals onto one event: ports, tasks,
 * spaces... state changes on all of them can appear as a bit in an event.
 */

#ifndef KERNEL_IPC_OBSERVER_H
#define KERNEL_IPC_OBSERVER_H

#include "event.h"
#include "observer_types.h"
#include <stdint.h>
#include <zuzu/err.h>

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
 * @param[in] is_event Set to true if the added observer is an event
 *
 * @return Err
 * @retval ERR_NOMEM if the observer set is full.
 */
Err ObserverAdd(ObserverSet *s, EventObject *ev, uint32_t bit, bool is_event);

/**
 * @brief Returns true if the observer set doesn't hold any Events.
 *
 * @param[in] s The set to check
 *
 * @return bool
 */
bool ObserverEmpty(const ObserverSet *s);

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
