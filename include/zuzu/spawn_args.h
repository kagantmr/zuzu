#ifndef ZUZU_SPAWN_ARGS_H
#define ZUZU_SPAWN_ARGS_H

#include "memprot.h"
#include "zuzu/types.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    uint32_t size;      /* wrapper sets it */
    const char *name;   // name of the process (null-terminated string)
    size_t name_len;    // length of the name string (excluding null terminator)
    const char *argbuf; // "arg0\0arg1\0arg2\0"
    size_t argbuf_len;  // length of the argbuf (including null terminators)
    size_t argc;        // number of arguments in argbuf
} CreateSpawnArgs;

typedef struct
{
    uint32_t size;     /* sizeof(KickstartArgs); wrapper sets it */
    VirtAddr entry;    // entry point address in the target task's address space
    VirtAddr sp;       // stack pointer value for the target task
    uint32_t r0_val;   // value to set in register r0 of the target task
    uint32_t r1_val;   // value to set in register r1 of the target task
} HandleCntlStartArgs;

#ifdef __cplusplus
}
#endif

#endif // ZUZU_SPAWN_ARGS_H
