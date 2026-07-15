#ifndef ZUZU_SPAWN_ARGS_H
#define ZUZU_SPAWN_ARGS_H

#include <stdint.h>
#include <stddef.h>
#include "zuzu/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    uint32_t size;        /* sizeof(spawn_args_t); wrapper sets it */
    const void *elf_data; // pointer to ELF file data in memory
    size_t elf_size;      // size of ELF file data in bytes
    const char *name;     // name of the process (null-terminated string)
    size_t name_len;      // length of the name string (excluding null terminator)
    const char *argbuf;   // "arg0\0arg1\0arg2\0"
    size_t argbuf_len;    // length of the argbuf (including null terminators)
    uint32_t argc;        // number of arguments in argbuf
} spawn_args_t;

typedef struct
{
    uint32_t size;        /* sizeof(asinject_args_t); wrapper sets it */
    handle_t task_handle; // handle of the target task
    uintptr_t dst_va;     // destination virtual address in the target task's address space
    const void *src_buf;  // pointer to the source buffer in the current task's address space
    size_t len;           // length of the source buffer in bytes
    uint32_t prot;        // memory protection flags for the destination mapping (e.g., PROT_READ | PROT_WRITE)
} asinject_args_t;

typedef struct
{
    uint32_t size;        /* sizeof(kickstart_args_t); wrapper sets it */
    handle_t task_handle; // handle of the target task
    uintptr_t entry;      // entry point address in the target task's address space
    uintptr_t sp;         // stack pointer value for the target task
    uint32_t r0_val;      // value to set in register r0 of the target task
    uint32_t r1_val;      // value to set in register r1 of the target task
} kickstart_args_t;

#ifdef __cplusplus
}
#endif

#endif // ZUZU_SPAWN_ARGS_H