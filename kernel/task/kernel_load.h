#ifndef KERNEL_TASK_KERNEL_LOAD_H
#define KERNEL_TASK_KERNEL_LOAD_H

#include "kernel/space/space.h"
#include <stddef.h>
#include <stdint.h>
#include <types.h>

SpaceObject *KernelSpaceLoad(const void *zxf_data, size_t zxf_size, const char *name,
                             const char *argbuf, size_t argbuf_len, uint32_t argc,
                             bool leave_frozen);

#endif /* KERNEL_TASK_KERNEL_LOAD_H */
