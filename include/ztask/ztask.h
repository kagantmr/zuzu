#ifndef ZUZU_ZTASK_ZTASK_H
#define ZUZU_ZTASK_ZTASK_H

#include "zuzu/err.h"
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define ZTASK_DEFAULT_STACK (16 * 1024)

typedef struct ZTask {
    Handle handle;
    void *base;
    size_t size;
    int detached;
} ZTask;

ZTask *ZTaskSpawn(void (*entry)(void), size_t stack_size);

Err ZTaskSpawnDetached(void (*entry)(void), size_t stack_size);

Err ZTaskJoin(ZTask *t, Duration timeout, int32_t *exit_status);

#endif /* ZUZU_ZTASK_ZTASK_H */
