#ifndef ZUZU_PROCX_H
#define ZUZU_PROCX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "spawn_args.h"
#include <stdint.h>

typedef struct {
    handle_t  task_handle;
    zpid_t pid;
    int      alive;
} zprocx_handle_t;

zprocx_handle_t zprocx_spawn(const char *name, const spawn_args_t *args);
int             zprocx_wait(zprocx_handle_t *h, uint32_t timeout_ms);  // returns exit status
int             zprocx_kill(zprocx_handle_t *h);
int             zprocx_alive(const zprocx_handle_t *h);

#ifdef __cplusplus
}
#endif

#endif // ZUZU_PROCX_H