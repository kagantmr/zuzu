/**
 * @file zuzu.h
 * @brief zuzu system call wrappers
 */

#ifndef _LIB_ZUZU_H
#define _LIB_ZUZU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "svc_nums.h"
#include <arch/regs.h>
#include <arch/svc.h>
#include <stdint.h>
#include <string.h>
#include <types.h>
#include <zuzu/err.h>

/**
 * @addtogroup syscalls_misc
 * @{
 */

/**
 * @brief Ends the calling task. Never returns.
 *
 * The task becomes a zombie and @p exit_status is reported to whoever waits
 * on it: WaitOnTask() returns it as the value with outcome TASK_EXITED, and
 * events bound with EVENT_TASK fire. If this was the last live task in its
 * space, the space becomes hollow and WaitOnSpace() returns the same status.
 * The task's memory is reclaimed once the last handle to it is closed (or,
 * for the last task, when its space is destroyed).
 *
 * @syscall_num SVC_QUIT
 * @syscall_group misc
 * @syscall_blocking no
 * @syscall_sig (exit_status) -> does not return
 * @param[in] exit_status Status to report; by convention ZUZU_OK or an Err.
 */
static inline void Quit(Err exit_status) { ArchInvokeSvc(SVC_QUIT, exit_status, 0, 0, 0); }

/**
 * @brief Gives up the rest of the caller's timeslice.
 *
 * The caller goes to the back of its priority level and runs again once the
 * other ready tasks at that level have had their turn. Returns at once if no
 * other task is ready.
 *
 * @syscall_num SVC_YIELD
 * @syscall_group misc
 * @syscall_blocking no
 * @syscall_sig () -> r0 ZUZU_OK
 */
static inline void Yield(void) { ArchInvokeSvc(SVC_YIELD, 0, 0, 0, 0); }

/**
 * @brief Blocks the calling task for at least @p ms milliseconds.
 *
 * Other tasks run in the meantime. The wake-up is accurate to the counter,
 * not to a tick; it can be late by however long higher-priority tasks keep
 * the CPU.
 *
 * @syscall_num SVC_SLEEP
 * @syscall_group misc
 * @syscall_blocking yes
 * @syscall_sig (ms) -> r0 status
 * @param[in] ms Time to sleep, in milliseconds.
 * @return Always ZUZU_OK.
 */
static inline Err Sleep(Duration ms) { return ArchInvokeSvc(SVC_SLEEP, (Register)ms, 0, 0, 0); }

/**
 * @brief Waits until @p h has something to report, or @p timeout milliseconds pass.
 *
 * The generic wait for ports, events, tasks and spaces; it returns as soon as
 * something arrives. What r1-r3 carry depends on the type of @p h:
 *
 * | Handle | r1                           | r2             | r3                           |
 * |--------|------------------------------|----------------|------------------------------|
 * | Port   | sender's marker              | message length | granted handle, or -1        |
 * | Event  | signalled bits (now cleared) |                |                              |
 * | Task   | exit status or fault reason  |                | TASK_EXITED or TASK_FAULTED  |
 * | Space  | last task's exit status      |                |                              |
 *
 * Receiving on a port leaves the caller holding the call: it must Reply() or
 * Hold() before receiving again. Waiting on a task that has already exited or
 * faulted returns at once with ZUZU_OK. WaitOnPort(), WaitOnEvent(),
 * WaitOnTask() and WaitOnSpace() unpack the result into typed structs.
 *
 * @syscall_num SVC_WAITON
 * @syscall_group misc
 * @syscall_blocking yes
 * @syscall_sig (h, timeout) -> r0 status, r1-r3 per handle type
 * @param[in] h       Port, event, task or space handle with PERM_WAIT.
 * @param[in] timeout Milliseconds to wait; TIMEOUT_POLL checks and returns at
 *                    once, TIMEOUT_INFINITE waits forever.
 * @return r0 is the status; r1-r3 as in the table above.
 * @retval ZUZU_OK       Something arrived.
 * @retval ERR_TIMEOUT   Nothing arrived within @p timeout (or nothing was there, for a poll).
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_NOPERM    @p h lacks PERM_WAIT.
 * @retval ERR_BADTYPE   @p h is not a port, event, task or space.
 * @retval ERR_DEAD      The port or event was destroyed, or the space was torn down.
 * @retval ERR_BUSY      Port only: the caller still holds an unanswered call.
 * @retval ERR_BADARG    @p h is the caller's own task or own space.
 */
static inline SvcResult WaitOn(Handle h, Duration timeout)
{
    return ArchInvokeSvc4(SVC_WAITON, h, (Register)timeout, 0, 0);
}

/**
 * @brief Tells whether a pointer returned by MemMap() or MemMapAnon() is an error.
 *
 * Those calls return either an address or a negative Err cast to a pointer;
 * the error range is the top 4095 bytes of the address space.
 *
 * @param[in] p Pointer to test.
 * @return Non-zero if @p p encodes an error; recover it with `(Err)(intptr_t)p`.
 */
static inline int PtrIsErr(const void *p) { return (VirtAddr)p >= (VirtAddr)(-4095); }

#ifdef CONFIG_ZUZU_BENCH
/**
 * @brief Clears the kernel's benchmark counters (CONFIG_ZUZU_BENCH builds only; a no-op otherwise).
 */
static inline void BenchReset(void) { ArchInvokeSvc(SVC_BENCH, BENCH_RESET, 0, 0, 0); }
/**
 * @brief Prints the kernel's benchmark counters to the kernel console
 *        (CONFIG_ZUZU_BENCH builds only; a no-op otherwise).
 */
static inline void BenchDump(void) { ArchInvokeSvc(SVC_BENCH, BENCH_DUMP, 0, 0, 0); }
#else
static inline void BenchReset(void) {}
static inline void BenchDump(void) {}
#endif

/** @} */

/**
 * @addtogroup syscalls_handle
 * @{
 */

/**
 * @brief Creates a second handle to the same object, with the same or fewer permissions.
 *
 * @p marker is only applied when @p h is a port owned by the caller's space;
 * a server uses it to tell its callers apart. Any other handle keeps its
 * current marker. HandleDup() is the easier form.
 *
 * @syscall_num SVC_MANAGEHANDLE / MNGHNDL_DUPLICATE
 * @syscall_group handle
 * @syscall_blocking no
 * @syscall_sig (h, perms, marker) -> r0 status, r1 new handle
 * @param[in] h      Handle to duplicate.
 * @param[in] perms  Permissions for the copy; intersected with those of @p h.
 * @param[in] marker Marker for the copy (own ports only).
 * @return r0 is the status; r1 the new handle on success.
 * @retval ZUZU_OK       The new handle is in r1.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_NOMEM     The caller's handle table is full.
 */
static inline SvcResult HandleDuplicate(Handle h, HandlePerms perms, Marker marker)
{
    return ArchInvokeSvc4(SVC_MANAGEHANDLE, h, MNGHNDL_DUPLICATE, perms, (Register)marker);
}

/**
 * @brief Removes permissions from a handle. Permissions can only be dropped, never added.
 *
 * @syscall_num SVC_MANAGEHANDLE / MNGHNDL_RESTRICT
 * @syscall_group handle
 * @syscall_blocking no
 * @syscall_sig (h, mask) -> r0 status
 * @param[in] h    Handle to restrict.
 * @param[in] mask Permissions to keep; everything outside it is removed.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BUSY Can't restrict because @p h is mapped, unmap or MemProtect before you Restrict.
 */
static inline Err HandleRestrict(Handle h, HandlePerms mask)
{
    return ArchInvokeSvc(SVC_MANAGEHANDLE, h, MNGHNDL_RESTRICT, mask, 0);
}

/**
 * @brief Closes a handle.
 *
 * The object lives on while other handles or mappings refer to it; closing
 * the last handle to a port kills the port, failing its waiting callers with
 * ERR_DEAD.
 *
 * @syscall_num SVC_MANAGEHANDLE / MNGHNDL_CLOSE
 * @syscall_group handle
 * @syscall_blocking no
 * @syscall_sig (h) -> r0 status
 * @param[in] h Handle to close.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADARG    @p h names the caller's own space or one of its ancestors.
 */
static inline Err HandleClose(Handle h)
{
    return ArchInvokeSvc(SVC_MANAGEHANDLE, h, MNGHNDL_CLOSE, 0, 0);
}

/**
 * @brief Closes a handle and, for a space, tears the space down.
 *
 * Destroying a space kills all its tasks and releases everything it holds.
 * For any other object this is the same as HandleClose().
 *
 * @syscall_num SVC_MANAGEHANDLE / MNGHNDL_DESTROY
 * @syscall_group handle
 * @syscall_blocking no
 * @syscall_sig (h) -> r0 status
 * @param[in] h Handle to destroy.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADARG    @p h names the caller's own space or one of its ancestors.
 */
static inline Err HandleDestroy(Handle h)
{
    return ArchInvokeSvc(SVC_MANAGEHANDLE, h, MNGHNDL_DESTROY, 0, 0);
}

/**
 * @brief Reads one property of a handle or the object behind it.
 *
 * | @p what          | r1                                                  |
 * |------------------|-----------------------------------------------------|
 * | QUERY_TYPE       | handle type                                         |
 * | QUERY_PERMS      | the handle's permissions                            |
 * | QUERY_MARKER     | the handle's marker                                 |
 * | QUERY_STATUS     | task exit status, space's last exit status, or ZUZU_OK / ERR_DEAD for a port |
 * | QUERY_SIZE       | size in bytes of a memory object                    |
 * | QUERY_DMA_COUNT  | number of DMA mappings on a device                  |
 * | QUERY_DMA_ID     | id of the @p index-th mapping, 0 = newest           |
 * | QUERY_DMA_BUS / _LEN / _DIR | bus address, length, direction of mapping @p index (an id) |
 *
 * @syscall_num SVC_MANAGEHANDLE / MNGHNDL_QUERY
 * @syscall_group handle
 * @syscall_blocking no
 * @syscall_sig (h, what, index) -> r0 status, r1 value
 * @param[in] h     Handle to inspect.
 * @param[in] what  Property to read.
 * @param[in] index QUERY_DMA_ID: position; QUERY_DMA_BUS/_LEN/_DIR: mapping id; otherwise ignored.
 * @return r0 is the status; r1 the value on success.
 * @retval ZUZU_OK       The value is in r1.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADARG    @p what is not a known property.
 * @retval ERR_BADTYPE   The property does not apply to this kind of object.
 * @retval ERR_NOPERM    DMA queries need PERM_MAP on the device handle.
 * @retval ERR_NOENT     No DMA mapping at that position or with that id.
 */
static inline SvcResult HandleQueryAt(Handle h, QueryWhat what, size_t index)
{
    return ArchInvokeSvc4(SVC_MANAGEHANDLE, h, MNGHNDL_QUERY, what, (Register)index);
}

/**
 * @brief HandleQueryAt() for properties that take no index.
 *
 * @param[in] h    Handle to inspect.
 * @param[in] what Property to read.
 * @return As HandleQueryAt().
 */
static inline SvcResult HandleQuery(Handle h, QueryWhat what) { return HandleQueryAt(h, what, 0); }

/**
 * @brief Copies a handle into another space's handle table.
 *
 * The copy keeps the marker of @p h. The receiving space has to learn the new
 * handle's value some other way (by convention, or a message). HandleGrantTo()
 * is the easier form.
 *
 * @syscall_num SVC_MANAGEHANDLE / MNGHNDL_GRANT
 * @syscall_group handle
 * @syscall_blocking no
 * @syscall_sig (h, target_space, perms) -> r0 status, r1 handle in the target
 * @param[in] h                   Handle to grant, with PERM_TXFR.
 * @param[in] target_space_handle Space to receive it, with PERM_CNTL.
 * @param[in] perms               Permissions for the copy; intersected with those of @p h.
 * @return r0 is the status; r1 the handle's value in the target's table.
 * @retval ZUZU_OK       The target's handle value is in r1.
 * @retval ERR_BADHANDLE @p h or @p target_space_handle is not a live handle.
 * @retval ERR_BADTYPE   @p target_space_handle is not a space.
 * @retval ERR_NOPERM    @p target_space_handle lacks PERM_CNTL, or @p h lacks PERM_TXFR.
 * @retval ERR_DEAD      The target space has been torn down.
 * @retval ERR_NOMEM     The target's handle table is full.
 */
static inline SvcResult HandleGrant(Handle h, Handle target_space_handle, HandlePerms perms)
{
    return ArchInvokeSvc4(SVC_MANAGEHANDLE, h, MNGHNDL_GRANT, target_space_handle, perms);
}

/**
 * @brief HandleDuplicate() that stores the new handle in @p out.
 *
 * @param[in]  h      Handle to duplicate.
 * @param[in]  perms  Permissions for the copy.
 * @param[in]  marker Marker for the copy (own ports only).
 * @param[out] out    Receives the new handle on success; may be NULL.
 * @return As HandleDuplicate().
 * @see HandleDuplicate()
 */
static inline Err HandleDup(Handle h, HandlePerms perms, Marker marker, Handle *out)
{
    SvcResult r = HandleDuplicate(h, perms, marker);
    if (r.r0 == ZUZU_OK && out)
        *out = (Handle)r.r1;
    return (Err)r.r0;
}

/**
 * @brief HandleGrant() that stores the target's handle value in @p out.
 *
 * @param[in]  h                   Handle to grant.
 * @param[in]  target_space_handle Space to receive it, with PERM_CNTL.
 * @param[in]  perms               Permissions for the copy.
 * @param[out] out                 Receives the handle's value in the target; may be NULL.
 * @return As HandleGrant().
 * @see HandleGrant()
 */
static inline Err HandleGrantTo(Handle h, Handle target_space_handle, HandlePerms perms,
                                Handle *out)
{
    SvcResult r = HandleGrant(h, target_space_handle, perms);
    if (r.r0 == ZUZU_OK && out)
        *out = (Handle)r.r1;
    return (Err)r.r0;
}

/**
 * @brief HandleQuery() that stores the value in @p out.
 *
 * @param[in]  h    Handle to inspect.
 * @param[in]  what Property to read.
 * @param[out] out  Receives the value on success; may be NULL.
 * @return As HandleQueryAt().
 * @see HandleQueryAt()
 */
static inline Err HandleQueryValue(Handle h, QueryWhat what, uint32_t *out)
{
    SvcResult r = HandleQuery(h, what);
    if (r.r0 == ZUZU_OK && out)
        *out = (uint32_t)r.r1;
    return (Err)r.r0;
}

/**
 * @brief Reads the size in bytes of a memory object.
 *
 * @param[in]  h   Memory object handle.
 * @param[out] out Receives the size on success; may be NULL.
 * @return As HandleQueryAt() with QUERY_SIZE.
 */
static inline Err HandleQuerySize(Handle h, size_t *out)
{
    uint32_t v = 0;
    Err rc = HandleQueryValue(h, QUERY_SIZE, &v);
    if (rc == ZUZU_OK && out)
        *out = v;
    return rc;
}

/**
 * @brief Tells whether a handle is live and its object has not died.
 *
 * @param[in] h Handle to test.
 * @return true if @p h is a live handle whose QUERY_STATUS is not ERR_DEAD.
 */
static inline bool HandleIsAlive(Handle h)
{
    uint32_t st = 0;
    return HandleQueryValue(h, QUERY_STATUS, &st) == ZUZU_OK && (Err)st != ERR_DEAD;
}

/** @} */

/**
 * @addtogroup syscalls_task
 * @{
 */

/**
 * @brief Creates a task in a space. The task does not run until TaskStart().
 *
 * Its priority and maximum priority start at the caller's, capped by the
 * space's maximum.
 *
 * @syscall_num SVC_CREATE / OBJECT_TASK
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (space) -> r0 task handle or error
 * @param[in] space_handle Space to create it in, with PERM_CNTL; -1 for the caller's own space.
 * @return A task handle with all permissions, or a negative error code.
 * @retval ERR_BADHANDLE @p space_handle is not a live handle.
 * @retval ERR_BADTYPE   @p space_handle is not a space.
 * @retval ERR_NOPERM    @p space_handle lacks PERM_CNTL.
 * @retval ERR_DEAD      The space has been torn down.
 * @retval ERR_BUSY      The task could not be created (no kernel stack or task slot left).
 * @retval ERR_NOMEM     The caller's handle table is full.
 */
static inline Handle CreateTask(Handle space_handle)
{
    return ArchInvokeSvc(SVC_CREATE, OBJECT_TASK, space_handle, 0, 0);
}

/**
 * @brief Starts a task created by CreateTask().
 *
 * @syscall_num SVC_MANAGETASK / MNGTASK_START
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (task, &args) -> r0 status
 * @param[in] h     Task handle with PERM_CNTL.
 * @param[in] entry Address the task starts executing at.
 * @param[in] sp    Its initial stack pointer.
 * @param[in] r0    Value of r0 at entry.
 * @param[in] r1    Value of r1 at entry.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADTYPE   @p h is not a task.
 * @retval ERR_NOPERM    @p h lacks PERM_CNTL.
 * @retval ERR_BUSY      The task has already been started.
 */
static inline Err TaskStart(Handle h, void *entry, void *sp, uint32_t r0, uint32_t r1)
{
    KickstartArgs args = {
        .entry = entry,
        .sp = sp,
        .r0 = r0,
        .r1 = r1,
    };
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_START, (Register)(VirtAddr)&args, 0);
}

/**
 * @brief Terminates another task, as if it had called Quit(ERR_DEAD).
 *
 * Killing a faulted task also unfreezes its space. Killing a task that has
 * already exited succeeds and does nothing.
 *
 * @syscall_num SVC_MANAGETASK / MNGTASK_KILL
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (task) -> r0 status
 * @param[in] h Task handle with PERM_CNTL.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADTYPE   @p h is not a task.
 * @retval ERR_NOPERM    @p h lacks PERM_CNTL.
 * @retval ERR_BADARG    @p h is the caller itself or -1; use Quit() to end yourself.
 */
static inline Err TaskKill(Handle h)
{
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_KILL, 0, 0);
}

/**
 * @brief Sets a task's base priority. Higher numbers run first.
 *
 * The effective priority can be higher while the task serves a call from a
 * higher-priority caller (priority inheritance).
 *
 * @syscall_num SVC_MANAGETASK / MNGTASK_SET_PRIORITY
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (task, prio) -> r0 status
 * @param[in] h   Task handle with PERM_CNTL, or -1 for the caller.
 * @param[in] val New base priority.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_NOPERM    @p val is above the caller's maximum or the task's maximum,
 *                       or @p h lacks PERM_CNTL.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADTYPE   @p h is not a task.
 */
static inline Err TaskSetPriority(Handle h, uint32_t val)
{
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SET_PRIORITY, (Register)val, 0);
}

/**
 * @brief Sets the highest priority a task may give itself; lowers its base priority if needed.
 *
 * @syscall_num SVC_MANAGETASK / MNGTASK_SET_MAX_PRIO
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (task, max) -> r0 status
 * @param[in] h   Task handle with PERM_CNTL, or -1 for the caller.
 * @param[in] val New maximum priority.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_NOPERM    @p val is above the caller's own maximum, or @p h lacks PERM_CNTL.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADTYPE   @p h is not a task.
 */
static inline Err TaskSetMaxPriority(Handle h, uint32_t val)
{
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SET_MAX_PRIO, (Register)val, 0);
}

/**
 * @brief Sets how long a task runs before same-priority tasks get a turn.
 *
 * @syscall_num SVC_MANAGETASK / MNGTASK_SET_TIMESLICE
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (task, slice_us) -> r0 status
 * @param[in] h        Task handle with PERM_CNTL, or -1 for the caller.
 * @param[in] slice_us Slice length in microseconds, within [TIMESLICE_MIN_US, TIMESLICE_MAX_US].
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADARG    @p slice_us is out of range.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADTYPE   @p h is not a task.
 * @retval ERR_NOPERM    @p h lacks PERM_CNTL.
 */
static inline Err TaskSetTimeSlice(Handle h, uint32_t slice_us)
{
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SET_TIMESLICE, (Register)slice_us, 0);
}

/**
 * @brief Stops a task until TaskResume().
 *
 * A task blocked in a syscall stays blocked; if it is woken while suspended,
 * its result is delivered but it does not run until resumed. A task may
 * suspend itself. Suspending twice is not an error.
 *
 * @syscall_num SVC_MANAGETASK / MNGTASK_SUSPEND
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (task) -> r0 status
 * @param[in] h Task handle with PERM_CNTL.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADTYPE   @p h is not a task.
 * @retval ERR_NOPERM    @p h lacks PERM_CNTL.
 * @retval ERR_DEAD      The task has exited.
 * @retval ERR_BADARG    The task was never started or has faulted, or @p h is -1.
 */
static inline Err TaskSuspend(Handle h)
{
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SUSPEND, 0, 0);
}

/**
 * @brief Lets a suspended task run again, or continues a faulted task.
 *
 * Continuing a faulted task also unfreezes its space; fix its registers with
 * TaskSetRegs() first, or it faults again.
 *
 * @syscall_num SVC_MANAGETASK / MNGTASK_RESUME
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (task) -> r0 status
 * @param[in] h Task handle with PERM_CNTL.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADTYPE   @p h is not a task.
 * @retval ERR_NOPERM    @p h lacks PERM_CNTL.
 * @retval ERR_BADARG    The task is neither suspended nor faulted, or @p h is -1.
 */
static inline Err TaskResume(Handle h)
{
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_RESUME, 0, 0);
}

/**
 * @brief Reads the user registers of a faulted or suspended task.
 *
 * @syscall_num SVC_MANAGETASK / MNGTASK_GET_REGS
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (task, out) -> r0 status
 * @param[in]  h   Task handle with PERM_CNTL.
 * @param[out] out Receives the registers.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BUSY      The task is neither faulted nor suspended.
 * @retval ERR_DEAD      The task has exited.
 * @retval ERR_BADPTR    @p out is not writable memory.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADTYPE   @p h is not a task.
 * @retval ERR_NOPERM    @p h lacks PERM_CNTL.
 */
static inline Err TaskGetRegs(Handle h, TaskRegs *out)
{
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_GET_REGS, (Register)(VirtAddr)out, 0);
}

/**
 * @brief Writes the user registers of a faulted or suspended task.
 *
 * Privileged bits in @p in->flags are ignored, the task always resumes in
 * user mode with interrupts enabled.
 *
 * @syscall_num SVC_MANAGETASK / MNGTASK_SET_REGS
 * @syscall_group task
 * @syscall_blocking no
 * @syscall_sig (task, in) -> r0 status
 * @param[in] h  Task handle with PERM_CNTL.
 * @param[in] in New register values.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BUSY      The task is running, or suspended inside a blocking call.
 * @retval ERR_BADARG    @p in->pc is misaligned for the instruction set in @p in->flags.
 * @retval ERR_DEAD      The task has exited.
 * @retval ERR_BADPTR    @p in is not readable memory.
 * @retval ERR_BADHANDLE @p h is not a live handle.
 * @retval ERR_BADTYPE   @p h is not a task.
 * @retval ERR_NOPERM    @p h lacks PERM_CNTL.
 */
static inline Err TaskSetRegs(Handle h, const TaskRegs *in)
{
    return ArchInvokeSvc(SVC_MANAGETASK, h, MNGTASK_SET_REGS, (Register)(VirtAddr)in, 0);
}

/**
 * @brief Unpacks the registers of a WaitOn() on a task.
 *
 * @param[in] r Result of WaitOn() on a task handle.
 * @return The status, the outcome (TASK_EXITED or TASK_FAULTED) and the exit
 *         status or fault reason.
 */
static inline TaskWaitResult FormatToTaskWait(SvcResult r)
{
    return (TaskWaitResult){
        .status = (Err)r.r0,
        .outcome = (TaskWaitOutcome)r.r3,
        .value = (int32_t)r.r1,
    };
}

/**
 * @brief Waits for a task to exit or fault.
 *
 * @param[in] task    Task handle with PERM_WAIT; not the caller itself.
 * @param[in] timeout Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 * @return The unpacked result; status as for WaitOn().
 * @see WaitOn()
 */
static inline TaskWaitResult WaitOnTask(Handle task, Duration timeout)
{
    return FormatToTaskWait(WaitOn(task, timeout));
}

/** @} */

/**
 * @addtogroup syscalls_space
 * @{
 */

/**
 * @brief Creates an empty space.
 *
 * Fill it with MemInjectObj() / MemInjectBytes(), then add tasks with
 * CreateTask(). Its maximum priority is the caller's.
 *
 * @syscall_num SVC_CREATE / OBJECT_SPACE
 * @syscall_group space
 * @syscall_blocking no
 * @syscall_sig (name, len) -> r0 space handle or error
 * @param[in] name Name for diagnostics; truncated to 31 characters.
 * @return A space handle with all permissions, or a negative error code.
 * @retval ERR_BADPTR @p name is not readable memory.
 * @retval ERR_NOMEM  Out of kernel memory, or the caller's handle table is full.
 */
static inline Handle CreateSpace(const char *name)
{
    return ArchInvokeSvc(SVC_CREATE, OBJECT_SPACE, (VirtAddr)name, strlen(name), 0);
}

/**
 * @brief Unpacks the registers of a WaitOn() on a space.
 *
 * @param[in] r Result of WaitOn() on a space handle.
 * @return The status and the exit status of the space's last task.
 */
static inline SpaceWaitResult FormatToSpaceWait(SvcResult r)
{
    return (SpaceWaitResult){
        .status = (Err)r.r0,
        .exit_status = (Err)r.r1,
    };
}

/**
 * @brief Waits until a space has no live tasks left.
 *
 * @param[in] space   Space handle with PERM_WAIT; not the caller's own space.
 * @param[in] timeout Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 * @return The unpacked result; status as for WaitOn().
 * @see WaitOn()
 */
static inline SpaceWaitResult WaitOnSpace(Handle space, Duration timeout)
{
    return FormatToSpaceWait(WaitOn(space, timeout));
}

/** @} */

/**
 * @addtogroup syscalls_memory
 * @{
 */

/**
 * @brief Creates a memory object that can be mapped, shared, injected and used for DMA.
 *
 * Without flags, pages are allocated on first touch. MEM_CONTIG allocates
 * physically contiguous, zeroed pages up front (required for DMA);
 * MEM_UNCACHED makes mappings uncached and needs MEM_CONTIG.
 *
 * @syscall_num SVC_CREATE / OBJECT_MEMORY
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (pages, flags) -> r0 memory handle or error
 * @param[in] page_count Size in pages; must be at least 1.
 * @param[in] flags      MEM_CONTIG and/or MEM_UNCACHED, or 0.
 * @return A memory handle with all permissions, or a negative error code.
 * @retval ERR_BADARG @p page_count is 0, @p flags has unknown bits, or MEM_UNCACHED without MEM_CONTIG.
 * @retval ERR_NOMEM  Not enough free (or contiguous) memory, or the handle table is full.
 */
static inline Handle CreateMem(size_t page_count, CreateMemoryFlags flags)
{
    return ArchInvokeSvc(SVC_CREATE, OBJECT_MEMORY, (Register)page_count, flags, 0);
}

/**
 * @brief Maps a memory object into the caller's address space.
 *
 * A handle can back one mapping at a time; duplicate it to map the object twice.
 *
 * @syscall_num SVC_MANAGEMEMORY / MNGMEM_MAP
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (mem, hint | prot) -> r0 address or error
 * @param[in] mem_handle Memory object handle with PERM_MAP, plus PERM_WRITE to map writable and
 *                       PERM_EXEC to map executable.
 * @param[in] hint_va    Page-aligned address to map at, or 0 to let the kernel choose.
 * @param[in] prot       PROT_READ / PROT_WRITE / PROT_EXEC, but never writable and executable.
 * @return The mapped address, or an error encoded as a pointer; check with PtrIsErr().
 * @retval ERR_BADHANDLE @p mem_handle is not a live handle.
 * @retval ERR_BADTYPE   @p mem_handle is not a memory object.
 * @retval ERR_NOPERM    @p mem_handle lacks PERM_MAP, or the PERM_WRITE / PERM_EXEC @p prot asks
 *                       for, or PROT_EXEC on uncached memory.
 * @retval ERR_BUSY      This handle is already mapped.
 * @retval ERR_BADARG    @p hint_va is unusable, or @p prot is both writable and executable.
 * @retval ERR_NOMEM     No address space or page tables left.
 */
static inline void *MemMap(Handle mem_handle, VirtAddr hint_va, MemProt prot)
{
    uint32_t packed = (hint_va & ~0xFFFU) | ((uint32_t)prot & 0x7U);
    return (void *)ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_MAP, mem_handle, 0, (Register)packed);
}

/**
 * @brief Maps fresh zeroed memory, backed by no object, into the caller's address space.
 *
 * Pages are allocated on first touch.
 *
 * @syscall_num SVC_MANAGEMEMORY / MNGMEM_MAP (HANDLE_ANON)
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (HANDLE_ANON, size, hint | prot) -> r0 address or error
 * @param[in] size    Size in bytes.
 * @param[in] hint_va Page-aligned address to map at, or 0 to let the kernel choose.
 * @param[in] prot    PROT_READ / PROT_WRITE / PROT_EXEC.
 * @return The mapped address, or an error encoded as a pointer; check with PtrIsErr().
 * @retval ERR_BADARG   @p size or @p hint_va is unusable.
 * @retval ERR_OVERFLOW @p size is too large.
 * @retval ERR_NOMEM    No address space left.
 */
static inline void *MemMapAnon(size_t size, VirtAddr hint_va, MemProt prot)
{
    uint32_t packed = (hint_va & ~0xFFFU) | ((uint32_t)prot & 0x7U);
    return (void *)ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_MAP, HANDLE_ANON, (Register)size,
                                 (Register)packed);
}

/**
 * @brief Removes a mapping made by MemMap() or MemMapAnon().
 *
 * @syscall_num SVC_MANAGEMEMORY / MNGMEM_UNMAP
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (va) -> r0 status
 * @param[in] va Start address of the mapping.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_NOENT  No mapping contains @p va.
 * @retval ERR_BADARG @p va is not the start of its mapping.
 * @retval ERR_NOPERM The mapping is pinned (for example the syspage).
 */
static inline Err MemUnmap(void *va)
{
    return ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_UNMAP, (Register)va, 0, 0);
}

/**
 * @brief Changes the protection of a range of the caller's mapped memory.
 *
 * @syscall_num SVC_MANAGEMEMORY / MNGMEM_PROTECT
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (va, size, prot) -> r0 status
 * @param[in] va       Start of the range, page-aligned.
 * @param[in] size     Length in bytes.
 * @param[in] new_prot PROT_READ / PROT_WRITE / PROT_EXEC, but never writable and executable.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_NOPERM @p new_prot is beyond what the mapping was allowed when it was made: the
 *                    PERM_WRITE / PERM_EXEC of the handle it came from, or what the injector chose.
 * @retval ERR_BADARG The range is not mapped, not aligned, or the protection is not allowed.
 */
static inline Err MemProtect(VirtAddr va, size_t size, MemProt new_prot)
{
    return ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_PROTECT, (Register)va, (Register)size, new_prot);
}

/**
 * @brief Maps part of a memory object into another space, typically one being set up.
 *
 * The mapping holds its own reference, so the handle can be closed afterwards.
 * Pass HANDLE_ANON as @p mem_handle for demand-zero memory.
 *
 * @syscall_num SVC_MANAGEMEMORY / MNGMEM_INJECTOBJ
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (space, &args) -> r0 status
 * @param[in] kitten_space_handle Target space with PERM_CNTL.
 * @param[in] mem_handle          Memory object in the caller's space, or HANDLE_ANON.
 * @param[in] dest_vaddr          Page-aligned address in the target space.
 * @param[in] offset              Byte offset into the object; ignored for HANDLE_ANON.
 * @param[in] size                Bytes to map.
 * @param[in] prot                Protection of the target mapping, never writable and
 *                                executable. It also becomes the ceiling MemProtect() cannot
 *                                raise in the target space.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE A handle is not live.
 * @retval ERR_BADTYPE   @p kitten_space_handle is not a space, or @p mem_handle is not memory.
 * @retval ERR_NOPERM    Missing PERM_CNTL on the space or PERM_MAP on the object, or @p prot
 *                       needs a PERM_WRITE / PERM_EXEC the object handle lacks.
 * @retval ERR_DEAD      The target space has been torn down.
 * @retval ERR_BADARG    Misaligned or out-of-range addresses, offset or size.
 * @retval ERR_BUSY      The target range is already mapped.
 * @retval ERR_NOMEM     Out of memory or page tables.
 */
static inline Err MemInjectObj(Handle kitten_space_handle, Handle mem_handle, VirtAddr dest_vaddr,
                               size_t offset, size_t size, MemProt prot)
{
    InjectObjArgs args = {
        .mem = mem_handle,
        .dest_vaddr = dest_vaddr,
        .offset = offset,
        .len = size,
        .prot = prot,
    };
    return ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_INJECTOBJ, kitten_space_handle,
                         (Register)(VirtAddr)&args, 0);
}

/**
 * @brief Copies bytes into another space at a fixed address.
 *
 * Creates a temporary memory object of whole pages, copies @p len bytes in,
 * zero-fills the rest and injects it with MemInjectObj().
 *
 * @param[in] kitten_space_handle Target space with PERM_CNTL.
 * @param[in] va                  Page-aligned address in the target space.
 * @param[in] src                 Bytes to copy.
 * @param[in] len                 Number of bytes.
 * @param[in] prot                Protection of the target mapping.
 * @return ZUZU_OK, or an error from CreateMem(), MemMap() or MemInjectObj().
 */
static inline Err MemInjectBytes(Handle kitten_space_handle, VirtAddr va, const void *src,
                                 size_t len, MemProt prot)
{
    size_t pages = (len + 0xFFFU) >> 12;
    Handle obj = CreateMem(pages, 0);
    if (obj < 0)
        return (Err)obj;

    uint8_t *w = (uint8_t *)MemMap(obj, 0, PROT_RW);
    if (PtrIsErr(w)) {
        HandleClose(obj);
        return (Err)(intptr_t)w;
    }
    memcpy(w, src, len);
    memset(w + len, 0, (pages << 12) - len);
    MemUnmap(w);

    Err rc = MemInjectObj(kitten_space_handle, obj, va, 0, pages << 12, prot);
    HandleClose(obj);
    return rc;
}

/**
 * @brief Unpacks the registers of a DMA map call.
 *
 * @param[in] r Result registers of MNGMEM_DMAMAP.
 * @return The status, the bus address and the mapping id.
 */
static inline DmaMapResult FormatToDmaMap(SvcResult r)
{
    return (DmaMapResult){
        .status = (Err)r.r0,
        .bus_addr = (uintptr_t)r.r1,
        .id = (uint32_t)r.r2,
    };
}

/**
 * @brief Lets a device reach part of a memory object by DMA.
 *
 * Cached memory is cleaned and invalidated at map time; use DmaSync() around
 * later transfers. The mapping belongs to the device, not the caller, so it
 * survives the driver exiting; DmaAdopt() lets a restarted driver take it over.
 *
 * @syscall_num SVC_MANAGEMEMORY / MNGMEM_DMAMAP
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (&args) -> r0 status, r1 bus address, r2 id
 * @param[in] dev    Device handle with PERM_MAP.
 * @param[in] mem    MEM_CONTIG memory object with PERM_MAP.
 * @param[in] offset Page-aligned byte offset into @p mem.
 * @param[in] len    Page-aligned length; must fit in @p mem.
 * @param[in] dir    Direction of the transfers.
 * @return The status, the address the device must use, and the mapping id.
 * @retval ERR_BADHANDLE @p dev or @p mem is not a live handle.
 * @retval ERR_BADTYPE   @p dev is not a device, or @p mem is not MEM_CONTIG memory.
 * @retval ERR_NOPERM    A handle lacks PERM_MAP.
 * @retval ERR_BADARG    Bad direction, misaligned or out-of-range window, or beyond the device's DMA reach.
 * @retval ERR_NOMEM     The device has no mapping slots left.
 * @retval ERR_BADPTR    The argument block is unreadable.
 */
static inline DmaMapResult DmaMap(Handle dev, Handle mem, size_t offset, size_t len, DmaDir dir)
{
    DmaMapArgs args = {.dev = dev, .mem = mem, .offset = offset, .len = len, .dir = dir};
    return FormatToDmaMap(
        ArchInvokeSvc4(SVC_MANAGEMEMORY, MNGMEM_DMAMAP, (Register)(VirtAddr)&args, 0, 0));
}

/**
 * @brief Removes a DMA mapping and releases its hold on the memory.
 *
 * @syscall_num SVC_MANAGEMEMORY / MNGMEM_DMAUNMAP
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (dev, id) -> r0 status
 * @param[in] dev Device handle with PERM_MAP.
 * @param[in] id  Mapping id from DmaMap().
 * @return ZUZU_OK, or an error code.
 * @retval ERR_NOENT     No mapping with that id.
 * @retval ERR_BADHANDLE @p dev is not a live handle.
 * @retval ERR_BADTYPE   @p dev is not a device.
 * @retval ERR_NOPERM    @p dev lacks PERM_MAP.
 */
static inline Err DmaUnmap(Handle dev, uint32_t id)
{
    return ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_DMAUNMAP, dev, (Register)id, 0);
}

/**
 * @brief Returns a fresh handle to the memory behind an existing DMA mapping.
 *
 * For a restarted driver to take over buffers its predecessor mapped. The new
 * handle has PERM_MAP only.
 *
 * @syscall_num SVC_MANAGEMEMORY / MNGMEM_DMAADOPT
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (dev, id) -> r0 status, r1 memory handle
 * @param[in] dev Device handle with PERM_MAP.
 * @param[in] id  Mapping id, e.g. from HandleQueryAt() with QUERY_DMA_ID.
 * @return The new memory handle, or a negative error code.
 * @retval ERR_NOENT     No mapping with that id.
 * @retval ERR_BADHANDLE @p dev is not a live handle.
 * @retval ERR_BADTYPE   @p dev is not a device.
 * @retval ERR_NOPERM    @p dev lacks PERM_MAP.
 * @retval ERR_NOMEM     The caller's handle table is full.
 */
static inline Handle DmaAdopt(Handle dev, uint32_t id)
{
    SvcResult r = ArchInvokeSvc4(SVC_MANAGEMEMORY, MNGMEM_DMAADOPT, dev, (Register)id, 0);
    return (r.r0 == 0) ? (Handle)r.r1 : (Handle)r.r0;
}

/**
 * @brief Makes cached memory and a device agree before or after a transfer.
 *
 * DMA_SYNC_FOR_DEVICE writes the CPU's cached data back before the device
 * reads; DMA_SYNC_FOR_CPU discards stale cache lines before the CPU reads
 * what the device wrote. Not needed for MEM_UNCACHED memory.
 *
 * @syscall_num SVC_MANAGEMEMORY / MNGMEM_DMASYNC
 * @syscall_group memory
 * @syscall_blocking no
 * @syscall_sig (dev, bus | op, len) -> r0 status
 * @param[in] dev      Device handle with PERM_MAP.
 * @param[in] bus_addr Start of the range, inside one of the device's mappings.
 * @param[in] len      Length in bytes.
 * @param[in] op       DMA_SYNC_FOR_DEVICE or DMA_SYNC_FOR_CPU.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_NOENT     The range is not inside a mapping of @p dev.
 * @retval ERR_BADARG    Bad range or operation.
 * @retval ERR_BADHANDLE @p dev is not a live handle.
 * @retval ERR_BADTYPE   @p dev is not a device.
 * @retval ERR_NOPERM    @p dev lacks PERM_MAP.
 */
static inline Err DmaSync(Handle dev, uintptr_t bus_addr, size_t len, DmaSyncOp op)
{
    return ArchInvokeSvc(SVC_MANAGEMEMORY, MNGMEM_DMASYNC, dev, (Register)(bus_addr | op),
                         (Register)len);
}

/** @} */

/**
 * @addtogroup syscalls_ipc
 * @{
 */

/**
 * @brief Creates a port: something callers Call() and a server receives on.
 *
 * @syscall_num SVC_CREATE / OBJECT_PORT
 * @syscall_group ipc
 * @syscall_blocking no
 * @syscall_sig () -> r0 port handle or error
 * @return A port handle with all permissions, or a negative error code.
 * @retval ERR_NOMEM Out of kernel memory, or the caller's handle table is full.
 */
static inline Handle CreatePort(void) { return ArchInvokeSvc(SVC_CREATE, OBJECT_PORT, 0, 0, 0); }

/**
 * @brief Sends a message to a port and blocks until the server replies, or until a timeout.
 *
 * The request is the first @p xlen bytes of the caller's message box; the
 * reply arrives in the same place. Callers queue by priority, and the server
 * runs at the caller's priority while serving (priority inheritance).
 *
 * The timeout covers the whole call: waiting to be received and waiting for the reply. When it
 * expires the call is withdrawn. A server that already took it finds its reply capability gone:
 * a late Reply() returns ERR_BADHANDLE, and a late ReplyRecv() just waits for the next call, so
 * the stale answer cannot be mistaken for the reply to a later call.
 *
 * @syscall_num SVC_CALL
 * @syscall_group ipc
 * @syscall_blocking yes
 * @syscall_sig (port, xlen, grant, timeout) -> r0 status, r1 reply length, r3 granted handle
 * @param[in] port         Port handle with PERM_SEND.
 * @param[in] xlen         Request length in bytes, at most MSG_BUF_SIZE.
 * @param[in] grant_handle Handle (with PERM_TXFR) to copy to the server, or -1.
 * @param[in] timeout      Milliseconds to wait for the reply, or TIMEOUT_INFINITE. TIMEOUT_POLL
 *                         always returns ERR_TIMEOUT, since a call cannot finish without blocking.
 * @return r0 is the status; r1 the reply length; r3 a handle granted back, or -1.
 * @retval ZUZU_OK       The reply is in the message box.
 * @retval ERR_TIMEOUT   No reply within @p timeout.
 * @retval ERR_OVERFLOW  @p xlen is larger than MSG_BUF_SIZE.
 * @retval ERR_BADHANDLE @p port or @p grant_handle is not a live handle.
 * @retval ERR_BADTYPE   @p port is not a port.
 * @retval ERR_NOPERM    @p port lacks PERM_SEND, or @p grant_handle lacks PERM_TXFR.
 * @retval ERR_DEAD      The port died, or the server died before replying.
 * @retval ERR_NOMEM     The server's handle table is full (when granting).
 */
static inline SvcResult CallTimeout(Handle port, uint32_t xlen, Handle grant_handle,
                                    Duration timeout)
{
    return ArchInvokeSvc4(SVC_CALL, port, (Register)xlen, grant_handle, (Register)timeout);
}

/**
 * @brief Call() with no timeout: blocks until the server replies.
 *
 * @param[in] port         Port handle with PERM_SEND.
 * @param[in] xlen         Request length in bytes, at most MSG_BUF_SIZE.
 * @param[in] grant_handle Handle (with PERM_TXFR) to copy to the server, or -1.
 * @return As CallTimeout().
 * @see CallTimeout()
 */
static inline SvcResult Call(Handle port, uint32_t xlen, Handle grant_handle)
{
    return CallTimeout(port, xlen, grant_handle, TIMEOUT_INFINITE);
}

/**
 * @brief Answers the call the caller is holding.
 *
 * The reply is the first @p xlen bytes of the server's message box. If the
 * caller's CallTimeout() expired after the call was received, the call has been
 * withdrawn and there is nothing to answer.
 *
 * @syscall_num SVC_REPLY
 * @syscall_group ipc
 * @syscall_blocking no
 * @syscall_sig (xlen, grant, -1, 0) -> r0 status
 * @param[in] xlen         Reply length in bytes, at most MSG_BUF_SIZE.
 * @param[in] grant_handle Handle (with PERM_TXFR) to copy to the caller, or -1.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_OVERFLOW  @p xlen is larger than MSG_BUF_SIZE.
 * @retval ERR_DEAD      The caller has died.
 * @retval ERR_BADHANDLE There is no call to answer (none was received, or the caller timed out
 *                       and withdrew it), or @p grant_handle is not live.
 * @retval ERR_NOPERM    @p grant_handle lacks PERM_TXFR; the caller is failed with this error.
 * @retval ERR_NOMEM     The caller's handle table is full; the caller is failed with this error.
 */
static inline Err Reply(uint32_t xlen, Handle grant_handle)
{
    return ArchInvokeSvc(SVC_REPLY, (Register)xlen, grant_handle, -1, 0);
}

/**
 * @brief Answers the call in hand (if any), then waits for the next one, in one syscall.
 *
 * The usual server loop. Returns like WaitOn() on @p recv_port.
 *
 * @syscall_num SVC_REPLY
 * @syscall_group ipc
 * @syscall_blocking yes
 * @syscall_sig (xlen, grant, port, timeout) -> r0 status, r1 marker, r2 length, r3 granted handle
 * @param[in] xlen         Reply length in bytes, at most MSG_BUF_SIZE.
 * @param[in] grant_handle Handle (with PERM_TXFR) to copy to the caller, or -1.
 * @param[in] recv_port    Port to receive on, with PERM_WAIT.
 * @param[in] timeout      Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 * @return r0 is the status; r1-r3 as for WaitOn() on a port.
 * @retval ZUZU_OK       A new call has been received.
 * @retval ERR_TIMEOUT   No call within @p timeout.
 * @retval ERR_OVERFLOW  @p xlen is larger than MSG_BUF_SIZE.
 * @retval ERR_BADHANDLE @p recv_port or @p grant_handle is not a live handle.
 * @retval ERR_BADTYPE   @p recv_port is not a port.
 * @retval ERR_NOPERM    @p recv_port lacks PERM_WAIT, or @p grant_handle lacks PERM_TXFR.
 * @retval ERR_DEAD      @p recv_port has died.
 */
static inline SvcResult ReplyRecv(uint32_t xlen, Handle grant_handle, Handle recv_port,
                                  Duration timeout)
{
    return ArchInvokeSvc4(SVC_REPLY, (Register)xlen, grant_handle, recv_port, (Register)timeout);
}

/**
 * @brief Unpacks the registers of a receive on a port.
 *
 * @param[in] r Result of WaitOn() or ReplyRecv() on a port.
 * @return The status, the sender's marker, the message length and any granted handle.
 */
static inline PortWaitResult FormatToPortWait(SvcResult r)
{
    return (PortWaitResult){
        .status = (Err)r.r0,
        .sender = (Marker)r.r1,
        .xlen = (uint32_t)r.r2,
        .granted = (Handle)r.r3,
    };
}

/**
 * @brief Waits for a call on a port.
 *
 * On success the caller holds the call and must Reply(), ReplyRecv() or Hold()
 * before receiving again.
 *
 * @param[in] port    Port handle with PERM_WAIT.
 * @param[in] timeout Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 * @return The unpacked result; status as for WaitOn().
 * @see WaitOn()
 */
static inline PortWaitResult WaitOnPort(Handle port, Duration timeout)
{
    return FormatToPortWait(WaitOn(port, timeout));
}

/**
 * @brief CallTimeout() that stores the reply length and any granted handle.
 *
 * @param[in]  port         Port handle with PERM_SEND.
 * @param[in]  xlen         Request length in bytes.
 * @param[in]  grant_handle Handle to copy to the server, or -1.
 * @param[in]  timeout      Milliseconds to wait for the reply, or TIMEOUT_INFINITE.
 * @param[out] reply_len    Receives the reply length; may be NULL.
 * @param[out] reply_grant  Receives a handle granted back, or -1; may be NULL.
 * @return As CallTimeout().
 * @see CallTimeout()
 */
static inline Err CallPortTimeout(Handle port, uint32_t xlen, Handle grant_handle,
                                  Duration timeout, uint32_t *reply_len, Handle *reply_grant)
{
    SvcResult r = CallTimeout(port, xlen, grant_handle, timeout);
    if (r.r0 != ZUZU_OK)
        return (Err)r.r0;
    if (reply_len)
        *reply_len = (uint32_t)r.r1;
    if (reply_grant)
        *reply_grant = (Handle)r.r3;
    return ZUZU_OK;
}

/**
 * @brief Call() that stores the reply length and any granted handle.
 *
 * @param[in]  port         Port handle with PERM_SEND.
 * @param[in]  xlen         Request length in bytes.
 * @param[in]  grant_handle Handle to copy to the server, or -1.
 * @param[out] reply_len    Receives the reply length; may be NULL.
 * @param[out] reply_grant  Receives a handle granted back, or -1; may be NULL.
 * @return As Call().
 * @see Call()
 */
static inline Err CallPort(Handle port, uint32_t xlen, Handle grant_handle, uint32_t *reply_len,
                           Handle *reply_grant)
{
    return CallPortTimeout(port, xlen, grant_handle, TIMEOUT_INFINITE, reply_len, reply_grant);
}

/**
 * @brief ReplyRecv() with the result unpacked.
 *
 * @param[in] xlen         Reply length in bytes.
 * @param[in] grant_handle Handle to copy to the caller, or -1.
 * @param[in] recv_port    Port to receive on, with PERM_WAIT.
 * @param[in] timeout      Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 * @return The unpacked result; status as for ReplyRecv().
 * @see ReplyRecv()
 */
static inline PortWaitResult ReplyRecvPort(uint32_t xlen, Handle grant_handle, Handle recv_port,
                                           Duration timeout)
{
    return FormatToPortWait(ReplyRecv(xlen, grant_handle, recv_port, timeout));
}

/**
 * @brief Parks the call in hand so the server can receive others, and returns a ticket for it.
 *
 * The caller stays blocked until the ticket is answered with ReplyTo() (from
 * any thread of this space) or the port dies (ERR_DEAD). A parked caller no
 * longer lends its priority to the server. A port parks at most 64 calls.
 *
 * @syscall_num SVC_HOLD
 * @syscall_group ipc
 * @syscall_blocking no
 * @syscall_sig () -> r0 status, r1 ticket
 * @param[out] out Receives the ticket on success; may be NULL.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADARG No call in hand.
 * @retval ERR_DEAD   The caller died before it could be parked.
 * @retval ERR_NOMEM  The port's parking table is full.
 */
static inline Err Hold(Ticket *out)
{
    SvcResult r = ArchInvokeSvc4(SVC_HOLD, 0, 0, 0, 0);
    if (r.r0 == ZUZU_OK && out)
        *out = (Ticket)r.r1;
    return (Err)r.r0;
}

/**
 * @brief Answers a call parked with Hold().
 *
 * The reply is the first @p xlen bytes of the answering thread's message box.
 * Any thread of the space that parked the call may answer; a ticket answers
 * once and is meaningless in other spaces. The call in hand, if any, is not
 * affected.
 *
 * @syscall_num SVC_REPLY (ticket in r3)
 * @syscall_group ipc
 * @syscall_blocking no
 * @syscall_sig (xlen, grant, -1, ticket) -> r0 status
 * @param[in] t     Ticket from Hold().
 * @param[in] xlen  Reply length in bytes, at most MSG_BUF_SIZE.
 * @param[in] grant Handle (with PERM_TXFR) to copy to the caller, or -1.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_NOENT     Unknown, already answered or forged ticket, or the caller has died.
 * @retval ERR_BADHANDLE The handle the call was received through has been closed.
 * @retval ERR_NOPERM    That handle lacks PERM_WAIT, or @p grant lacks PERM_TXFR.
 * @retval ERR_OVERFLOW  @p xlen is larger than MSG_BUF_SIZE.
 */
static inline Err ReplyTo(Ticket t, uint32_t xlen, Handle grant)
{
    return ArchInvokeSvc(SVC_REPLY, (Register)xlen, grant, -1, (Register)t);
}

/** @} */

/**
 * @addtogroup syscalls_event
 * @{
 */

/**
 * @brief Creates an event: 31 bits that can be signalled and waited on.
 *
 * @syscall_num SVC_CREATE / OBJECT_EVENT
 * @syscall_group event
 * @syscall_blocking no
 * @syscall_sig () -> r0 event handle or error
 * @return An event handle with all permissions, or a negative error code.
 * @retval ERR_NOMEM Out of kernel memory, or the caller's handle table is full.
 */
static inline Handle CreateEvent(void) { return ArchInvokeSvc(SVC_CREATE, OBJECT_EVENT, 0, 0, 0); }

/**
 * @brief Sets bits on an event and wakes waiters.
 *
 * Without @p broadcast, the highest-priority waiter wakes and receives the
 * accumulated bits; with it, all waiters wake. With no waiter, the bits stay
 * set for the next WaitOn().
 *
 * @syscall_num SVC_SIGNAL
 * @syscall_group event
 * @syscall_blocking no
 * @syscall_sig (event, bits, flags) -> r0 status
 * @param[in] ev_handle Event handle with PERM_SEND.
 * @param[in] bits      Bits to set; bit 31 is reserved.
 * @param[in] broadcast Wake every waiter instead of one.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADARG    Bit 31 is set.
 * @retval ERR_BADHANDLE @p ev_handle is not a live handle.
 * @retval ERR_NOPERM    @p ev_handle lacks PERM_SEND, or a bit is claimed by a binding.
 * @retval ERR_BADTYPE   @p ev_handle is not an event.
 * @retval ERR_DEAD      The event has been destroyed.
 */
static inline Err Signal(Handle ev_handle, EventWord bits, bool broadcast)
{
    return ArchInvokeSvc(SVC_SIGNAL, ev_handle, (Register)bits, broadcast ? SIGNAL_BROADCAST : 0,
                         0);
}

/**
 * @brief Has the kernel set an event bit when a port, task or space needs attention.
 *
 * | @p type     | @p target | The bit is set when                    |
 * |-------------|-----------|----------------------------------------|
 * | EVENT_PORT  | port      | a caller is queued with nobody receiving |
 * | EVENT_TASK  | task      | the task exits or faults               |
 * | EVENT_SPACE | space     | the space has no live tasks left       |
 *
 * If the condition already holds, the bit is set at once. The bit then
 * belongs to the kernel: Signal() may not set it. An object accepts at most
 * four bindings.
 *
 * @syscall_num SVC_BIND / EVENT_PORT, EVENT_TASK, EVENT_SPACE
 * @syscall_group event
 * @syscall_blocking no
 * @syscall_sig (type, event, target, bit) -> r0 status
 * @param[in] type      EVENT_PORT, EVENT_TASK or EVENT_SPACE.
 * @param[in] ev_handle Event handle with PERM_CNTL.
 * @param[in] target    Handle of the matching kind, with PERM_WAIT.
 * @param[in] bit       Bit number, 0-30.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE A handle is not live.
 * @retval ERR_BADTYPE   @p ev_handle is not an event, or @p target is the wrong kind.
 * @retval ERR_NOPERM    @p ev_handle lacks PERM_CNTL, or @p target lacks PERM_WAIT.
 * @retval ERR_BADARG    @p bit is out of range, or @p type is unknown.
 * @retval ERR_DEAD      The event or the port has died.
 * @retval ERR_NOMEM     @p target already has four bindings.
 */
static inline Err Bind(EventType type, Handle ev_handle, Handle target, uint32_t bit)
{
    return ArchInvokeSvc(SVC_BIND, type, ev_handle, target, (Register)bit);
}

/**
 * @brief Delivers a device's interrupt as an event bit.
 *
 * The kernel masks the interrupt line each time it fires; call IrqRearm()
 * once the device's interrupt source has been cleared.
 *
 * @syscall_num SVC_BIND / EVENT_IRQ
 * @syscall_group event
 * @syscall_blocking no
 * @syscall_sig (EVENT_IRQ, event, device, bit) -> r0 status
 * @param[in] ev_handle  Event handle with PERM_CNTL.
 * @param[in] dev_handle Device handle with PERM_MAP whose interrupt to take.
 * @param[in] bit        Bit number, 0-30.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE A handle is not live.
 * @retval ERR_BADTYPE   @p ev_handle is not an event, or @p dev_handle is not a device.
 * @retval ERR_NOPERM    @p ev_handle lacks PERM_CNTL, or @p dev_handle lacks PERM_MAP.
 * @retval ERR_BADARG    The device has no interrupt, or @p bit is out of range.
 * @retval ERR_BUSY      Another space has already bound the interrupt.
 * @retval ERR_DEAD      The event has been destroyed.
 */
static inline Err BindIrq(Handle ev_handle, Handle dev_handle, uint32_t bit)
{
    return ArchInvokeSvc(SVC_BIND, EVENT_IRQ, ev_handle, dev_handle, (Register)bit);
}

/**
 * @brief Subscribes an event to the kernel's memory-pressure notifications.
 *
 * @syscall_num SVC_BIND / EVENT_MEMMGMT
 * @syscall_group event
 * @syscall_blocking no
 * @syscall_sig (EVENT_MEMMGMT, event) -> r0 status
 * @param[in] ev_handle Event handle with PERM_CNTL.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE @p ev_handle is not a live handle.
 * @retval ERR_BADTYPE   @p ev_handle is not an event.
 * @retval ERR_NOPERM    @p ev_handle lacks PERM_CNTL.
 * @retval ERR_DEAD      The event has been destroyed.
 * @retval ERR_BADARG    The event is already subscribed.
 * @retval ERR_NOMEM     No subscription slots left.
 */
static inline Err BindMemMgmt(Handle ev_handle)
{
    return ArchInvokeSvc(SVC_BIND, EVENT_MEMMGMT, ev_handle, 0, 0);
}

/**
 * @brief Unmasks a device's interrupt line after BindIrq() delivered it.
 *
 * Clear the interrupt in the device first, or it fires again immediately.
 *
 * @syscall_num SVC_MANAGEHANDLE / MNGHNDL_IRQ_REARM
 * @syscall_group event
 * @syscall_blocking no
 * @syscall_sig (device) -> r0 status
 * @param[in] dev_handle Device handle with PERM_MAP whose interrupt this space bound.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADHANDLE @p dev_handle is not a live handle.
 * @retval ERR_BADTYPE   @p dev_handle is not a device.
 * @retval ERR_BADARG    The device has no interrupt.
 * @retval ERR_NOPERM    @p dev_handle lacks PERM_MAP, or this space did not bind the interrupt.
 */
static inline Err IrqRearm(Handle dev_handle)
{
    return ArchInvokeSvc(SVC_MANAGEHANDLE, dev_handle, MNGHNDL_IRQ_REARM, 0, 0);
}

/**
 * @brief Unpacks the registers of a WaitOn() on an event.
 *
 * @param[in] r Result of WaitOn() on an event handle.
 * @return The status and the bits that were set.
 */
static inline EventWaitResult FormatToEventWait(SvcResult r)
{
    return (EventWaitResult){
        .status = (Err)r.r0,
        .bits = (EventWord)r.r1,
    };
}

/**
 * @brief Waits for bits on an event, then clears them.
 *
 * @param[in] ev      Event handle with PERM_WAIT.
 * @param[in] timeout Milliseconds to wait, or TIMEOUT_POLL / TIMEOUT_INFINITE.
 * @return The unpacked result; status as for WaitOn().
 * @see WaitOn()
 */
static inline EventWaitResult WaitOnEvent(Handle ev, Duration timeout)
{
    return FormatToEventWait(WaitOn(ev, timeout));
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* _LIB_ZUZU_H */
