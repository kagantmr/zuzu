#ifndef SYS_DEV_H
#define SYS_DEV_H

#include <arch/regs.h>

void mapdev(exception_frame_t *frame);
void querydev(exception_frame_t *frame);

#endif
