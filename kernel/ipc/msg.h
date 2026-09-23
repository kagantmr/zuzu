#ifndef _ZUZU_KERNEL_IPC_MSG_H
#define _ZUZU_KERNEL_IPC_MSG_H

#include "kernel/task/task.h"

void LmsgBufCopy(TaskObject *restrict src, TaskObject *restrict dst, size_t len);

#endif /* _ZUZU_KERNEL_IPC_MSG_H */