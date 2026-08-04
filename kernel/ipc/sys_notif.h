#ifndef SYS_NOTIF_H
#define SYS_NOTIF_H

#include <arch/regs.h>
#include <zuzu/types.h>
#include "kernel/ipc/ntfn.h"

struct thread_wait_slot;

void SysNtfnCreate(CpuState *frame);
void SysNtfnSignal(CpuState *frame);
void SysNtfnWait(CpuState *frame);

/* Wake one waiter whose slot has already been popped from ntfn->wait_queue.
 * r0_value lands in the waiter's r0 (delivered bits from ntfn_signal, or a
 * negative error from cap_destroy); bits is what a waitany waiter sees in
 * its result. A queued waiter without a trap frame is a corrupt wait queue:
 * panics rather than limp past it. */
void NtfnWakeWaiter(Ntfn *ntfn, struct thread_wait_slot *slot,
                      int32_t r0_value, NtfnBits bits);

#endif // SYS_NOTIF_H