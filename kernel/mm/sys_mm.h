#ifndef SYS_MM_H
#define SYS_MM_H

#include "arch/arm/include/context.h"
#include <stddef.h>

void memmap(exception_frame_t *frame);
void memunmap(exception_frame_t *frame);
void memshare(exception_frame_t *frame);
void attach(exception_frame_t *frame);
void detach(exception_frame_t *frame);

void asinject(exception_frame_t *frame);

#endif // SYS_MM_H