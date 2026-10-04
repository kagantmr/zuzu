/* sem.h - zuzuOS notification-based semaphore library. */

#ifndef ZUZU_SYNC_SEM
#define ZUZU_SYNC_SEM

#include "zuzu/err.h"
#include <stdatomic.h>
#include <types.h>

typedef struct sem {
    _Atomic int count;
    Handle event; /* kernel event object waiters block on */
} Semaphore;

Err SemInit(Semaphore *s, int initial_count);

Err SemDestroy(Semaphore *s);

Err SemPost(Semaphore *s);

Err SemWait(Semaphore *s);

#endif /* ZUZU_SYNC_SEM */
