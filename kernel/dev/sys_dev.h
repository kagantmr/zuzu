#ifndef SYS_DEV_H
#define SYS_DEV_H

#include "arch/arm/include/context.h"

void getdev(exception_frame_t *frame);
void enumdev(exception_frame_t *frame);
void mapdev(exception_frame_t *frame);

#endif