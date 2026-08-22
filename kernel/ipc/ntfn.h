#ifndef NOTIF_H
#define NOTIF_H

#include <list.h>
#include <stdbool.h>

#include <zuzu/types.h>

typedef struct Notification {
    NtfnBits word;       // 31-bit signal mask (bit 31 reserved), atomic-ish (IRQs off)
    ListHead wait_queue; // processes blocked in ntfn_wait
    Pid owner_pid;
    size_t ref_count;
    bool alive;
} NtfnObj;

struct thread_wait_slot;

/* Wake one waiter whose slot has already been popped from ntfn->wait_queue.
 * r0_value lands in the waiter's r0 (delivered bits from ntfn_signal, or a
 * negative error from cap_destroy); bits is what a waitany waiter sees in
 * its result. A queued waiter without a trap frame is a corrupt wait queue:
 * panics rather than limp past it. */
void NtfnWakeWaiter(NtfnObj *ntfn, struct thread_wait_slot *slot, int32_t r0_value, NtfnBits bits);

/**
 * Signal one or more bits on a notification object.
 *
 * ORs @p bits into the notification's word and wakes at most one waiter.
 * If a waiter is woken, it receives the accumulated word and the word is
 * cleared. If no waiter is queued, bits stay pending for the next wait.
 *
 * @param ntfn  Live notification object. Must not be NULL.
 * @param bits  Bits to signal. Bit 31 is reserved and must be zero.
 * @pre         Caller has verified @p ntfn is alive and @p bits is valid.
 * @pre         IRQs disabled.
 */
void NtfnSignal(NtfnObj *ntfn, NtfnBits bits);

void NtfnRefDrop(NtfnObj *ntfn);

#endif // NOTIF_H
