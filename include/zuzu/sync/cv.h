/* cv.h - zuzuOS notification-based condvar library. */

#ifndef ZUZU_SYNC_CV
#define ZUZU_SYNC_CV

#include "zuzu/err.h"
#include <zuzu/types.h>
#include <zuzu/sync/zone.h>
#include <zuzu/sync/sem.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct cv {
    _Atomic uint32_t seq;      /* bumped by every signal/broadcast */
    _Atomic int      waiters;
    Semaphore        sem;      /* waiters block here; count starts 0 */
} CondVariable;

Err CondVarInit(CondVariable* cv);

Err CondVarDestroy(CondVariable *cv);

Err CondVarSignal(CondVariable *cv);

Err CondVarBroadcast(CondVariable *cv);

/* Caller must hold `z` on entry; it is released while blocked and
 * re-acquired before return. Signal/Broadcast must be called with `z`
 * held so `waiters` is observed accurately (Mesa semantics). */
Err CondVarWait(CondVariable *cv, Zone *z);

#endif /* ZUZU_SYNC_CV */
