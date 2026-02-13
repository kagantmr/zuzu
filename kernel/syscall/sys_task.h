#ifndef SYSCALL_TASK_H
#define SYSCALL_TASK_H

#include "arch/arm/include/context.h"

void sys_task_quit(exception_frame_t *frame);
void sys_task_yield(exception_frame_t *frame);
void sys_log(exception_frame_t *frame);


#endif // SYSCALL_TASK_H