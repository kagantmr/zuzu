#ifndef SYS_NOTIF_H
#define SYS_NOTIF_H

#include <arch/regs.h>
#include "kernel/ipc/ntfn.h"

struct thread_wait_slot;

void ntfn_create(arch_regs_t *frame);
void ntfn_signal(arch_regs_t *frame);
void ntfn_wait(arch_regs_t *frame);

/* Wake one waiter whose slot has already been popped from ntfn->wait_queue.
 * r0_value lands in the waiter's r0 (delivered bits from ntfn_signal, or a
 * negative error from cap_destroy); bits is what a waitany waiter sees in
 * its result. A queued waiter without a trap frame is a corrupt wait queue:
 * panics rather than limp past it. */
void ntfn_wake_waiter(notification_t *ntfn, struct thread_wait_slot *slot,
                      int32_t r0_value, uint32_t bits);

#endif // SYS_NOTIF_H