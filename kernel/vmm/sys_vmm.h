#ifndef SYS_VMM_H
#define SYS_VMM_H

#include "arch/arm/include/context.h"

void memmap(exception_frame_t *frame);
void memunmap(exception_frame_t *frame);
void memshare(exception_frame_t *frame);
void attach(exception_frame_t *frame);
void mapdev(exception_frame_t *frame);


#endif // SYS_TASK_H