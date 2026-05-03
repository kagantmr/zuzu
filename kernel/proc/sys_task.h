#ifndef SYS_TASK_H
#define SYS_TASK_H

#include "arch/arm/include/context.h"

void quit(exception_frame_t *frame);
void yield(exception_frame_t *frame);
void sleep(exception_frame_t *frame);
void get_pid(exception_frame_t *frame);
void wait(exception_frame_t *frame);

void tspawn(exception_frame_t *frame);
void kickstart(exception_frame_t *frame);
void kill(exception_frame_t *frame);


#endif // SYS_TASK_H