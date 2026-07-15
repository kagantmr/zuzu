#ifndef SYS_PORT_H
#define SYS_PORT_H

#define NAMETABLE_PID 1

#include <arch/regs.h>

void sys_port_create(arch_regs_t *frame);
void sys_destroy(arch_regs_t *frame);
void sys_grant(arch_regs_t *frame);

#endif