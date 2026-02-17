#ifndef SYS_PORT_H
#define SYS_PORT_H

#include "arch/arm/include/context.h"

void sys_port_create(exception_frame_t *frame);
void sys_port_destroy(exception_frame_t *frame);
void sys_port_grant(exception_frame_t *frame);

#endif