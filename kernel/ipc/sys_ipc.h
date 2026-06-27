#ifndef SYS_IPC_H
#define SYS_IPC_H

#include <arch/regs.h>


void proc_send(arch_regs_t *frame);
void proc_recv(arch_regs_t *frame);
void proc_call(arch_regs_t *frame);
void proc_reply(arch_regs_t *frame);

void proc_sendx(arch_regs_t *frame);
void proc_callx(arch_regs_t *frame);
void proc_replyx(arch_regs_t *frame);
void proc_recvany(arch_regs_t *frame);


#endif // SYS_IPC_H