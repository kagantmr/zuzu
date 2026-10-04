#ifndef KERNEL_IPC_OBSERVER_H
#define KERNEL_IPC_OBSERVER_H

#include "event.h"
#include <stdint.h>
#include <zuzu/err.h>

#define MAX_OBSERVERS 4

typedef struct
{
    EventObject *ev;
    uint8_t bit;
} Observer;

/* The events that want to hear about a Task, Space or Port. Each observer
 * holds a reference on its event and a claim on its bit (bound_mask). */
typedef struct
{
    Observer slot[MAX_OBSERVERS];
} ObserverSet;

void ObserverInit(ObserverSet *s);

/* Same event: just moves its bit, no new reference. Otherwise takes a free
 * slot, first reclaiming observers whose event has died. ERR_NOMEM if full. */
Err ObserverAdd(ObserverSet *s, EventObject *ev, uint32_t bit);

/* Signals each live observer's bit once and drops dead ones. */
void ObserverNotify(ObserverSet *s);

/* Drops every observer and its event reference. */
void ObserverClear(ObserverSet *s);

#endif /* KERNEL_IPC_OBSERVER_H */
