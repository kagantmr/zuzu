#ifndef LMSG_H
#define LMSG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include "tls.h"
#include <stdbool.h>
#include <zuzu/types.h>
#include <zuzu/err.h>

/**
 * @brief Accessor for the current thread's local message buffer.
 * 
 * @return void* Pointer to the local message buffer.
 * 
 */
static inline void *LmsgBuf(void) {
    return ZuzuTLS()->LmsgBuf;
}

/**
 * @brief Writes data to the current thread's local message buffer.
 * 
 * @param src Pointer to the source data to write.
 * @param len Length of the data to write in bytes.
 * 
 * @return Err Returns the number of bytes written on success, or a negative error code on failure (e.g., ERR_OVERFLOW if len exceeds LMSG_BUF_SIZE).
 */
static inline Err LmsgWrite(const void *src, size_t len) {
    if (len > LMSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(LmsgBuf(), src, len);
    return len;
}

/**
 * @brief Reads data from the current thread's local message buffer.
 * 
 * @param dst Pointer to the destination buffer where the data will be read into.
 * @param len Length of the data to read in bytes.
 * 
 * @return Err Returns the number of bytes read on success, or a negative error code on failure (e.g., ERR_OVERFLOW if len exceeds LMSG_BUF_SIZE).
 */
static inline Err LmsgRead(void *dst, size_t len) {
    if (len > LMSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(dst, LmsgBuf(), len);
    return len;
}

/* lmsg write cursor */
typedef struct {
    char    *buf;   /* == LmsgBuf() */
    uint32_t off;   /* bytes written so far */
    uint32_t cap;   /* LMSG_BUF_SIZE */
    bool     ovf;   /* set if any append would exceed cap */
} LmsgWriter;

static inline void LmsgWriterInit(LmsgWriter *w) {
    w->buf = LmsgBuf();
    w->off = 0;
    w->cap = LMSG_BUF_SIZE;
    w->ovf = false;
}

static inline void LmsgPutU32(LmsgWriter *w, uint32_t v) {
    if (w->off + 4 > w->cap) { w->ovf = true; return; }
    memcpy(w->buf + w->off, &v, 4);   /* alignment-safe, matches reader */
    w->off += 4;
}

static inline void LmsgPutStr(LmsgWriter *w, const char *s) {
    uint32_t n = (uint32_t)strlen(s) + 1;          /* include the NUL */
    if (w->off + n > w->cap) { w->ovf = true; return; }
    memcpy(w->buf + w->off, s, n);
    w->off += n;
}

#ifdef __cplusplus
}
#endif

#endif // LMSG_H
