#ifndef IPCX_H
#define IPCX_H

#define IPCX_BUF_VA 0x7FFFA000
#define IPCX_BUF_SIZE 4096

#define IPCX_BUF ((void *)IPCX_BUF_VA)

#include <stdint.h>
#include <mem.h>

static inline uint32_t ipcx_clamp_len(uint32_t len) {
    return len > IPCX_BUF_SIZE ? IPCX_BUF_SIZE : len;
}

static inline void *ipcx_buf(void) {
    return IPCX_BUF;
}
static inline uint32_t ipcx_write(const void *src, uint32_t len) {
    uint32_t clamped = ipcx_clamp_len(len);
    memcpy(IPCX_BUF, src, clamped);
    return clamped;
}

static inline uint32_t ipcx_read(void *dst, uint32_t len) {
    uint32_t clamped = ipcx_clamp_len(len);
    memcpy(dst, IPCX_BUF, clamped);
    return clamped;
}


#endif