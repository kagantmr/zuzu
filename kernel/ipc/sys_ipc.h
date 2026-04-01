#ifndef SYS_IPC_H
#define SYS_IPC_H

#include "arch/arm/include/context.h"


void proc_send(exception_frame_t *frame);
void proc_recv(exception_frame_t *frame);
void proc_call(exception_frame_t *frame);
void proc_reply(exception_frame_t *frame);

void proc_sendx(exception_frame_t *frame);
void proc_callx(exception_frame_t *frame);
void proc_replyx(exception_frame_t *frame);


#endif // SYS_IPC_H