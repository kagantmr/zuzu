#ifndef SYS_IPC_H
#define SYS_IPC_H

#include <arch/regs.h>


void sys_msg_send(arch_regs_t *frame);
void sys_msg_recv(arch_regs_t *frame);
void sys_msg_call(arch_regs_t *frame);
void sys_msg_reply(arch_regs_t *frame);

void sys_msg_lsend(arch_regs_t *frame);
void sys_msg_lcall(arch_regs_t *frame);
void sys_msg_lreply(arch_regs_t *frame);
void sys_waitany(arch_regs_t *frame);


#endif // SYS_IPC_H