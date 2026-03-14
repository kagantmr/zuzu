#ifndef SYS_PORT_H
#define SYS_PORT_H

#define NAMETABLE_PID 1

#include "arch/arm/include/context.h"

void port_create(exception_frame_t *frame);
void port_destroy(exception_frame_t *frame);
void port_grant(exception_frame_t *frame);

#endif