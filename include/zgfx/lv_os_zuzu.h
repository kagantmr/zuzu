/**
 * @file
 * @brief Types LVGL's OS layer (LV_OS_CUSTOM) uses, built on ztask threads and zones.
 */

#ifndef ZGFX_LV_OS_ZUZU_H
#define ZGFX_LV_OS_ZUZU_H

#include <stdbool.h>
#include <types.h>
#include <ztask/primitives.h>

/** @brief An LVGL thread. */
typedef struct {
    ZTask *task;                /**< The task running the thread. */
    void (*callback)(void *);   /**< Function the thread runs. */
    void *user_data;            /**< Argument passed to @c callback. */
} lv_thread_t;

/** @brief An LVGL mutex; recursive, since LVGL re-takes its own lock. */
typedef struct {
    Zone zone;  /**< The zone that provides the exclusion. */
    Tid owner;  /**< Thread holding the mutex, or 0. */
    int depth;  /**< How many times the owner has locked it. */
} lv_mutex_t;

/** @brief An LVGL signal one thread waits on and another raises. */
typedef struct {
    Zone zone;         /**< Protects @c v. */
    CondVariable cv;   /**< Waiting threads block here. */
    bool v;            /**< True once the signal has been raised and not yet consumed. */
} lv_thread_sync_t;

#endif /* ZGFX_LV_OS_ZUZU_H */
