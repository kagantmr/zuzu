#ifndef ZUZU_SYNC_CV
#define ZUZU_SYNC_CV

#include "zuzu/err.h"
#include <stdatomic.h>
#include <stdint.h>
#include <sync/sem.h>
#include <sync/zone.h>
#include <types.h>

typedef struct {
    _Atomic uint32_t seq;
    _Atomic int waiters;
    Semaphore sem;
} CondVariable;

Err CondVarInit(CondVariable *cv);

Err CondVarDestroy(CondVariable *cv);

Err CondVarSignal(CondVariable *cv);

Err CondVarBroadcast(CondVariable *cv);

Err CondVarWait(CondVariable *cv, Zone *z);

#endif /* ZUZU_SYNC_CV */
