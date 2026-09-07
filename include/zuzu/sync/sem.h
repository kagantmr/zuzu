/* sem.h - zuzuOS notification-based semaphore library. */

#ifndef ZUZU_SYNC_SEM
#define ZUZU_SYNC_SEM

#include "zuzu/err.h"
#include <zuzu/types.h>

typedef struct sem Semaphore;

Err SemInit(Semaphore* s);

Err SemDestroy(Semaphore *s);

Err SemPost(Semaphore *s);

Err SemWait(Semaphore *s);

#endif /* ZUZU_SYNC_SEM */