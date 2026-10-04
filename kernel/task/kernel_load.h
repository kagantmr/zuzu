#ifndef KERNEL_TASK_KERNEL_LOAD_H
#define KERNEL_TASK_KERNEL_LOAD_H

#include "kernel/space/space.h"
#include <stddef.h>
#include <stdint.h>
#include <types.h>

/**
 * @brief Create a Space with a single Task, load a ZXF image into it, and
 * set up its initial argv/entry/sp.
 *
 * @param leave_frozen If true, the Task is left TASK_STATE_FROZEN with no trap frame
 * set up; the caller is expected to kickstart it later.
 */
SpaceObject *KernelSpaceLoad(const void *zxf_data, size_t zxf_size, const char *name,
                             const char *argbuf, size_t argbuf_len, uint32_t argc,
                             bool leave_frozen);

#endif /* KERNEL_TASK_KERNEL_LOAD_H */
