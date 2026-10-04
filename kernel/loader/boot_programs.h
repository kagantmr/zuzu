#ifndef KERNEL_LOADER_BOOT_PROGRAMS_H
#define KERNEL_LOADER_BOOT_PROGRAMS_H

#include <stddef.h>
#include "types.h"

void CreateRootService(PhysAddr initrd_pa, size_t initrd_size);

#endif /* KERNEL_LOADER_BOOT_PROGRAMS_H */
