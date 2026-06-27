#ifndef SYS_NOTIF_H
#define SYS_NOTIF_H

#include <arch/regs.h>

void ntfn_create(exception_frame_t *frame);
void ntfn_signal(exception_frame_t *frame);
void ntfn_wait(exception_frame_t *frame);

#endif // SYS_NOTIF_H