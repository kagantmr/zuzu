#ifndef ZGFX_LV_OS_ZUZU_H
#define ZGFX_LV_OS_ZUZU_H

#include <stdbool.h>
#include <types.h>
#include <ztask/primitives.h>

typedef struct {
    ZTask *task;
    void (*callback)(void *);
    void *user_data;
} lv_thread_t;

typedef struct {
    Zone zone;
    Tid owner;
    int depth;
} lv_mutex_t;

typedef struct {
    Zone zone;
    CondVariable cv;
    bool v;
} lv_thread_sync_t;

#endif /* ZGFX_LV_OS_ZUZU_H */
