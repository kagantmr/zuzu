#ifndef SYS_DTB_H
#define SYS_DTB_H

#include "arch/arm/include/context.h"

void dtb_find(exception_frame_t *frame);
void dtb_prop(exception_frame_t *frame);
void dtb_reg(exception_frame_t *frame);

#endif
