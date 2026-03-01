#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "elf.h"
#include "kernel/proc/process.h"

/**
 * Creates a new process from the given ELF data. Returns NULL on failure.
 * The ELF file must be a valid ARM executable (not just an object file or shared library
 * - it must have an entry point and PT_LOAD segments). The process will be created in the TASK_READY state.
 * The caller is responsible for eventually calling process_free() on the returned process.
 */
process_t *process_create_from_elf(const void *elf_data, size_t elf_size, const char *name);

#endif // ELF_LOADER_H