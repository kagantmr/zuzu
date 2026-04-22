#ifndef SYS_NOTIF_H
#define SYS_NOTIF_H

#include "arch/arm/include/context.h"

void ntfn_create(exception_frame_t *frame);
void ntfn_signal(exception_frame_t *frame);
void ntfn_wait(exception_frame_t *frame);
void ntfn_poll(exception_frame_t *frame);

#endif // SYS_NOTIF_H