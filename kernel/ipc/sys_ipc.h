#ifndef SYS_IPC_H
#define SYS_IPC_H

#include <arch/regs.h>


void msg_send(arch_regs_t *frame);
void msg_recv(arch_regs_t *frame);
void msg_call(arch_regs_t *frame);
void msg_reply(arch_regs_t *frame);

void msg_lsend(arch_regs_t *frame);
void msg_lcall(arch_regs_t *frame);
void msg_lreply(arch_regs_t *frame);
void waitany(arch_regs_t *frame);


#endif // SYS_IPC_H