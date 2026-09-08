#include <zuzu/sync/sem.h>
#include <zuzu/ntfn.h>
#include <zuzu/cap.h>
#include <stdbool.h>
#include <zuzu/tls.h>
#include <stdatomic.h>

Err SemInit(Semaphore* s, int initial_count) {
    s->count = initial_count;
    s->ntfn = ZuzuNtfnCreate();
    if (s->ntfn < 0) {
        return s->ntfn;
    }
    return ZUZU_OK;
}

Err SemDestroy(Semaphore *s) {
    if (ZuzuDestroy(s->ntfn) != ZUZU_OK) return ERR_BUSY;
    s->count = 0;
    return ZUZU_OK;
}

Err SemPost(Semaphore *s) {
    atomic_fetch_add(&s->count, 1);
    ZuzuNtfnSignal(s->ntfn, 1);
    return ZUZU_OK;
}

Err SemWait(Semaphore *s) {
    while (1) {
        int expected = atomic_load(&s->count);
        while (expected > 0) {
            if (atomic_compare_exchange_weak(&s->count, &expected, expected - 1)) {
                return ZUZU_OK;   // got a unit, no syscall
            }
            // CAS failed but expected still > 0 → someone else raced us, retry with updated expected
        }
        // count is <= 0 → nothing available, block
        ZuzuNtfnWait(s->ntfn, TIMEOUT_INFINITE);
    }
}
