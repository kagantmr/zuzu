#ifndef SYS_VMM_H
#define SYS_VMM_H

#include "arch/arm/include/context.h"
#include <stddef.h>

void memmap(exception_frame_t *frame);
void memunmap(exception_frame_t *frame);
void memshare(exception_frame_t *frame);
void attach(exception_frame_t *frame);
void mapdev(exception_frame_t *frame);
void detach(exception_frame_t *frame);

void sys_pmm_getfree(exception_frame_t *frame);


#endif // SYS_VMM_H