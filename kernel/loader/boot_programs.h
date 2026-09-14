#ifndef KERNEL_LOADER_BOOT_PROGRAMS_H
#define KERNEL_LOADER_BOOT_PROGRAMS_H

#include <stddef.h>
#include "zuzu/types.h"

/* Read boot.manifest from the (already-initialized) initrd, load and spawn
 * every listed boot program, inject device caps into devmgr, and seed
 * devmgr's task handle into sysd's handle table.
 *
 * initrd_pa / initrd_size describe the bootloader-supplied initrd as a
 * physical address + size; PROC_FLAG_INIT (sysd) gets it mapped into its
 * own address space. Panics if boot.manifest is missing. */
void boot_programs_spawn_all(PhysAddr initrd_pa, size_t initrd_size);

#endif /* KERNEL_LOADER_BOOT_PROGRAMS_H */
