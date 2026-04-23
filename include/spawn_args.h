#ifndef ZUZU_SPAWN_ARGS_H
#define ZUZU_SPAWN_ARGS_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    const void *elf_data;
    size_t      elf_size;
    const char *name;
    size_t      name_len;
    const char *argbuf;     // "arg0\0arg1\0arg2\0"
    size_t      argbuf_len;
    uint32_t    argc;
} spawn_args_t;

typedef struct {
    uint32_t task_handle;
    uintptr_t dst_va;
    const void *src_buf;
    size_t len;
    uint32_t prot;
} asinject_args_t;

typedef struct {
    uint32_t task_handle;
    uintptr_t entry;
    uintptr_t sp;
    uint32_t r0_val;
    uint32_t r1_val;
} kickstart_args_t;


#endif // ZUZU_SPAWN_ARGS_H