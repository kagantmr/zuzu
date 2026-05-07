#ifndef ZUZU_TYPES_H
#define ZUZU_TYPES_H

#include <stdint.h>
#include "err.h"

typedef int32_t handle_t;
typedef uint32_t pid_t;
typedef uint32_t den_id_t;
typedef uint64_t tick_t;
typedef uintptr_t paddr_t;
typedef uintptr_t vaddr_t;
typedef uint32_t irq_t;

/* ---- Common IPC types ---- */

typedef struct {
    int32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
} zuzu_ipcmsg_t;

/* ---- Memory management types ---- */

typedef struct {
    int32_t handle;
    void *addr;
} shmem_result_t;

/* ---- Process spawn types ---- */

typedef struct {
    handle_t task_handle;
    pid_t pid;
} tspawn_result_t;

#endif // ZUZU_TYPES_H