/**
 * @file boot_programs.h
 * @brief Loading of the root service from the initrd at boot.
 */

#ifndef KERNEL_LOADER_BOOT_PROGRAMS_H
#define KERNEL_LOADER_BOOT_PROGRAMS_H

#include "types.h"
#include <stddef.h>

/**
 * @brief Load the root service named by the initrd's boot.manifest and make it runnable.
 *
 * The first non-comment line of the manifest names the program. The root service gets device memory objects, the initrd and the BootInfo page mapped read-only. Panics if the manifest is missing or empty.
 *
 * @param[in] initrd_pa Physical address of the initrd.
 * @param[in] initrd_size Size of the initrd in bytes.
 */
void CreateRootService(PhysAddr initrd_pa, size_t initrd_size);

#endif /* KERNEL_LOADER_BOOT_PROGRAMS_H */
