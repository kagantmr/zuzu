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

/* AsInjectArgs.flags */
#define ASINJECT_FLAG_RESERVE                                                                      \
0x1u /* reserve [DestVAddr, DestVAddr+len) as demand-zero                                      \
        * anon memory in the target AS; src_buf must                                             \
        * be NULL, no bytes are copied up front. */

typedef struct
{
    uint32_t size;       /* wrapper sets it */
    VirtAddr dest_vaddr; // destination virtual address in the target task's address space
    const void *src_buf; // pointer to the source buffer in the current task's address space
    size_t len;          // length of the source buffer in bytes
    MemProt prot;   // memory protection flags for the destination mapping (e.g., PROT_READ |
                    // PROT_WRITE)
    uint32_t flags; // ASINJECT_FLAG_* bits; 0 for the original copy-in behavior
} McntlInjectArgs;

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
