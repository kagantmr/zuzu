#include <stdatomic.h>
#include <sync/cv.h>
#include <sync/sem.h>
#include <sync/zone.h>

Err CondVarInit(CondVariable *cv)
{
    atomic_store(&cv->seq, 0);
    atomic_store(&cv->waiters, 0);
    return SemInit(&cv->sem, 0);
}

Err CondVarDestroy(CondVariable *cv) { return SemDestroy(&cv->sem); }

Err CondVarWait(CondVariable *cv, Zone *z)
{ // z held on entry
    uint32_t ticket = atomic_load(&cv->seq);
    atomic_fetch_add(&cv->waiters, 1);
    ZoneExit(z);
    while (atomic_load(&cv->seq) == ticket)
        SemWait(&cv->sem);
    atomic_fetch_sub(&cv->waiters, 1);
    ZoneEnter(z);
    return ZUZU_OK;
}

Err CondVarSignal(CondVariable *cv)
{ // z held by caller
    atomic_fetch_add(&cv->seq, 1);
    if (atomic_load(&cv->waiters) > 0)
        SemPost(&cv->sem);
    return ZUZU_OK;
}

Err CondVarBroadcast(CondVariable *cv)
{ // z held by caller
    atomic_fetch_add(&cv->seq, 1);
    int n = atomic_load(&cv->waiters);
    for (int i = 0; i < n; i++)
        SemPost(&cv->sem);
    return ZUZU_OK;
}
