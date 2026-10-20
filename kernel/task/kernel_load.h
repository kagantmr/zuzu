/**
 * @file kernel_load.h
 * @brief Loads a space from the kernel, used to bring up rootsvc.
 */

#ifndef KERNEL_TASK_KERNEL_LOAD_H
#define KERNEL_TASK_KERNEL_LOAD_H

#include "kernel/space/space.h"
#include <stddef.h>
#include <stdint.h>
#include <types.h>

/**
 * @brief Creates a space, injects its ZXF data into it, and passes arguments, then starts its initial task.
 * 
 * @param[in] zxf_data The ZXF executable of the program.
 * @param[in] zxf_size How large the ZXF data is.
 * @param[in] name Name of the space
 * @param[in] argbuf The argv buffer.
 * @param[in] argbuf_len Size of the argv buffer.
 * @param[in] argc Argument count.
 * @param[in] leave_frozen Task starts off in FROZEN state.
 * 
 * @return SpaceObject* 
 */
SpaceObject *KernelSpaceLoad(const void *zxf_data, size_t zxf_size, const char *name,
                             const char *argbuf, size_t argbuf_len, uint32_t argc,
                             bool leave_frozen);

#endif /* KERNEL_TASK_KERNEL_LOAD_H */
