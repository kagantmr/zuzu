#ifndef KERNEL_IPC_OBSERVER_H
#define KERNEL_IPC_OBSERVER_H

#include "event.h"
#include <stdint.h>
#include <zuzu/err.h>

#define MAX_OBSERVERS 4

typedef struct {
    EventObject *ev;
    uint8_t bit;
} Observer;

typedef struct {
    Observer slot[MAX_OBSERVERS];
} ObserverSet;

void ObserverInit(ObserverSet *s);

Err ObserverAdd(ObserverSet *s, EventObject *ev, uint32_t bit);

void ObserverNotify(ObserverSet *s);

void ObserverClear(ObserverSet *s);

#endif /* KERNEL_IPC_OBSERVER_H */
