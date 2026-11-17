#include "lv_os_private.h"

#if LV_USE_OS == LV_OS_CUSTOM

#include <util/tls.h>
#include <zuzu/zuzu.h>

static void GenericCallback(void *arg)
{
    lv_thread_t *thread = arg;
    thread->callback(thread->user_data);
}

lv_result_t lv_thread_init(lv_thread_t *thread, const char *const name, lv_thread_prio_t prio,
                           void (*callback)(void *), size_t stack_size, void *user_data)
{
    LV_UNUSED(name);
    LV_UNUSED(prio);
    thread->callback = callback;
    thread->user_data = user_data;
    thread->task = ZTaskSpawn(GenericCallback, thread, stack_size);
    return PtrIsErr(thread->task) ? LV_RESULT_INVALID : LV_RESULT_OK;
}

lv_result_t lv_thread_delete(lv_thread_t *thread)
{
    return ZTaskJoin(thread->task, TIMEOUT_INFINITE, NULL) == ZUZU_OK ? LV_RESULT_OK
                                                                      : LV_RESULT_INVALID;
}

lv_result_t lv_mutex_init(lv_mutex_t *mutex)
{
    mutex->owner = 0;
    mutex->depth = 0;
    return ZoneInit(&mutex->zone) == ZUZU_OK ? LV_RESULT_OK : LV_RESULT_INVALID;
}

lv_result_t lv_mutex_lock(lv_mutex_t *mutex)
{
    Tid self = GetTls()->tid;
    if (mutex->depth > 0 && mutex->owner == self) {
        mutex->depth++;
        return LV_RESULT_OK;
    }
    if (ZoneEnter(&mutex->zone) != ZUZU_OK)
        return LV_RESULT_INVALID;
    mutex->owner = self;
    mutex->depth = 1;
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_lock_isr(lv_mutex_t *mutex) { return lv_mutex_lock(mutex); }

lv_result_t lv_mutex_unlock(lv_mutex_t *mutex)
{
    if (mutex->depth == 0 || mutex->owner != GetTls()->tid)
        return LV_RESULT_INVALID;
    if (--mutex->depth == 0) {
        mutex->owner = 0;
        ZoneExit(&mutex->zone);
    }
    return LV_RESULT_OK;
}

lv_result_t lv_mutex_delete(lv_mutex_t *mutex)
{
    return ZoneDestroy(&mutex->zone) == ZUZU_OK ? LV_RESULT_OK : LV_RESULT_INVALID;
}

lv_result_t lv_thread_sync_init(lv_thread_sync_t *sync)
{
    sync->v = false;
    if (ZoneInit(&sync->zone) != ZUZU_OK)
        return LV_RESULT_INVALID;
    if (CondVarInit(&sync->cv) != ZUZU_OK) {
        ZoneDestroy(&sync->zone);
        return LV_RESULT_INVALID;
    }
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_wait(lv_thread_sync_t *sync)
{
    ZoneEnter(&sync->zone);
    while (!sync->v)
        CondVarWait(&sync->cv, &sync->zone);
    sync->v = false;
    ZoneExit(&sync->zone);
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_signal(lv_thread_sync_t *sync)
{
    ZoneEnter(&sync->zone);
    sync->v = true;
    CondVarSignal(&sync->cv);
    ZoneExit(&sync->zone);
    return LV_RESULT_OK;
}

lv_result_t lv_thread_sync_signal_isr(lv_thread_sync_t *sync)
{
    return lv_thread_sync_signal(sync);
}

lv_result_t lv_thread_sync_delete(lv_thread_sync_t *sync)
{
    CondVarDestroy(&sync->cv);
    ZoneDestroy(&sync->zone);
    return LV_RESULT_OK;
}

#endif
