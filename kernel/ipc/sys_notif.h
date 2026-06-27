#ifndef SYS_NOTIF_H
#define SYS_NOTIF_H

#include <arch/regs.h>

void ntfn_create(arch_regs_t *frame);
void ntfn_signal(arch_regs_t *frame);
void ntfn_wait(arch_regs_t *frame);

#endif // SYS_NOTIF_H