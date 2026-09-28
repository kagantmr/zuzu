#ifndef KERNEL_LOADER_BOOT_PROGRAMS_H
#define KERNEL_LOADER_BOOT_PROGRAMS_H

#include <stddef.h>
#include "zuzu/types.h"

void CreateRootSvc(PhysAddr initrd_pa, size_t initrd_size);

#endif /* KERNEL_LOADER_BOOT_PROGRAMS_H */
