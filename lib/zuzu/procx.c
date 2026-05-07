#include <zuzu/procx.h>
#include <string.h>

zprocx_handle_t zprocx_spawn(const char *name, const spawn_args_t *args) {
    (void)name; (void)args;
    zprocx_handle_t h;
    h.task_handle = -1;
    h.pid = 0;
    h.alive = 0;
    return h;
}

int zprocx_wait(zprocx_handle_t *h, uint32_t timeout_ms) {
    (void)h; (void)timeout_ms;
    return -1;
}

int zprocx_kill(zprocx_handle_t *h) {
    (void)h;
    return -1;
}

int zprocx_alive(const zprocx_handle_t *h) {
    return h && h->alive;
}
