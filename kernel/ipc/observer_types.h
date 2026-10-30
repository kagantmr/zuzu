/**
 * @file observer_types.h
 * 
 * @brief Observer type definitions.
 */
#ifndef KERNEL_IPC_OBSERVER_TYPES_H
#define KERNEL_IPC_OBSERVER_TYPES_H

#include <stdbool.h>
#include <stdint.h>

struct EventObjectStruct;

/** @brief Limit of observers on an object. */
#define MAX_OBSERVERS 4

/** @brief An observer: the event to signal and which bit. */
typedef struct {
    struct EventObjectStruct *ev; /**< The bound event object. */
    uint8_t bit;                  /**< The bit position it will signal. */
    bool from_event;              /**< The source is an Event, so ev->child_count counts it. */
} Observer;

/** @brief Set of observers for an object. */
typedef struct {
    Observer slot[MAX_OBSERVERS]; /**< List of observers. */
} ObserverSet;

#endif /* KERNEL_IPC_OBSERVER_TYPES_H */
