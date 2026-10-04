#ifndef KERNEL_LOADER_BOOT_PROGRAMS_H
#define KERNEL_LOADER_BOOT_PROGRAMS_H

#include "types.h"
#include <stddef.h>

void CreateRootService(PhysAddr initrd_pa, size_t initrd_size);

#endif /* KERNEL_LOADER_BOOT_PROGRAMS_H */
