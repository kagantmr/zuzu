/**
 * @file
 * @brief Tasks used as threads: spawn one on its own stack, then join it or let it clean up.
 *
 * The task's control block sits at the top of its stack mapping, so the stack is freed with one
 * unmap once the task has ended.
 */

#ifndef ZUZU_ZTASK_ZTASK_H
#define ZUZU_ZTASK_ZTASK_H

#include "zuzu/err.h"
#include <stddef.h>
#include <stdint.h>
#include <types.h>

/** @brief Stack size used when 0 is passed to ZTaskSpawn() or ZTaskSpawnDetached(). */
#define ZTASK_DEFAULT_STACK (16 * 1024)

/** @brief A task started by ZTaskSpawn(). */
typedef struct ZTask {
    Handle handle;         /**< The task handle. */
    void *base;            /**< Start of the stack mapping. */
    size_t size;           /**< Size of the stack mapping in bytes. */
    void (*entry)(void *); /**< Function the task runs. */
    void *arg;             /**< Argument passed to @c entry. */
    int detached;          /**< Non-zero if the task unmaps its own stack when it ends. */
} ZTask;

/**
 * @brief Starts a joinable task.
 *
 * When @p entry returns the task ends with status ZUZU_OK. Call ZTaskJoin() to wait for it and
 * free its stack.
 *
 * @param[in] entry      Function the task runs.
 * @param[in] arg        Argument passed to @p entry.
 * @param[in] stack_size Stack size in bytes, or 0 for ZTASK_DEFAULT_STACK.
 * @return The new task, or an error code encoded as a pointer (test with PtrIsErr()).
 * @retval ERR_BADARG @p stack_size is too small to hold the task's control block.
 */
ZTask *ZTaskSpawn(void (*entry)(void *), void *arg, size_t stack_size);

/**
 * @brief Starts a task that frees its own stack when it ends.
 *
 * The task cannot be joined, and the caller must not use it again.
 *
 * @param[in] entry      Function the task runs.
 * @param[in] arg        Argument passed to @p entry.
 * @param[in] stack_size Stack size in bytes, or 0 for ZTASK_DEFAULT_STACK.
 * @return ZUZU_OK, or the error that kept the task from starting.
 */
Err ZTaskSpawnDetached(void (*entry)(void *), void *arg, size_t stack_size);

/**
 * @brief Waits for a task to end, then frees its stack.
 *
 * Nothing is freed if the wait itself fails, so the call can be repeated.
 *
 * @param[in]  t           A task from ZTaskSpawn().
 * @param[in]  timeout     Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 * @param[out] exit_status Receives the task's exit status or fault reason; may be NULL.
 * @return ZUZU_OK if the task exited.
 * @retval ERR_DEAD    The task faulted; @p exit_status holds the reason.
 * @retval ERR_TIMEOUT The task had not ended within @p timeout.
 */
Err ZTaskJoin(ZTask *t, Duration timeout, int32_t *exit_status);

#endif /* ZUZU_ZTASK_ZTASK_H */
