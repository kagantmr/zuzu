#ifndef KERNEL_IPC_WAITSLOT_H
#define KERNEL_IPC_WAITSLOT_H

#include "kernel/proc/thread.h"

/**
 * @brief Register a thread's waitany wait set.
 *
 * Idempotent: unregisters any previous registration first, so the thread's
 * waitany state is always either 0 slots or exactly @p count slots, never
 * a partial mix.
 *
 * @param self  Thread to register.
 * @param slots Slots to register (kind/ntfn-or-port/handle_index already
 *              filled in). May be a local/stack array; copied internally.
 * @param count Number of entries in @p slots.
 */
void WaitSlotsRegister(Thread *self, const WaitSlot *slots, uint32_t count);

/**
 * @brief Unlink every registered waitany slot and reset bookkeeping.
 *
 * No-op if nothing is registered. Call before returning/retrying from
 * SysWaitAny, and from any path that delivers into or tears down a
 * waitany-blocked thread from outside it (SysMsgSend/Call/Lsend/Lcall,
 * relay_handler, SysIrqBind, NtfnWakeWaiter, SysDestroy, ProcessKill).
 *
 * @param t Thread to unregister.
 */
void WaitSlotsUnregisterAll(Thread *t);

/**
 * @brief Record a waitany match.
 *
 * Stores which handle matched and the full result. Does not touch
 * registration state or wake the thread. Callers still call
 * WaitSlotsUnregisterAll() and add the thread to the scheduler themselves.
 *
 * @param t            Thread that matched.
 * @param handle_index Index into the caller's handles[] array.
 * @param result       Full result to deliver; copied in.
 */
void WaitSlotsDeliver(Thread *t, uint32_t handle_index, const WaitanyResult *result);

/**
 * @brief Build a WAITANY_KIND_NTFN result.
 * @param matched_index Index into the caller's handles[] array.
 * @param bits          Delivered notification bits.
 * @param result        Filled in.
 */
void WaitanyDeliverNtfn(uint32_t matched_index, uint32_t bits, WaitanyResult *result);

#endif // KERNEL_IPC_WAITSLOT_H
