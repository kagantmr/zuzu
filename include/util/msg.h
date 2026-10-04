#ifndef ZUZU_MSG_H
#define ZUZU_MSG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tls.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <types.h>
#include <zuzu/err.h>

/**
 * @brief Accessor for the current thread's local message buffer. This is
 * the primary IPC payload transport: Call()/Reply() move exactly the bytes
 * written here, up to MSG_BUF_SIZE.
 *
 * @return void* Pointer to the message buffer.
 */
static inline void *MessageBuf(void) { return ZuzuTLS()->msg_buf; }

/**
 * @brief Writes data to the current thread's message buffer.
 *
 * @param src Pointer to the source data to write.
 * @param len Length of the data to write in bytes.
 *
 * @return Err Returns the number of bytes written on success, or a negative error code on failure
 * (e.g., ERR_OVERFLOW if len exceeds MSG_BUF_SIZE).
 */
static inline Err MsgWrite(const void *src, size_t len)
{
    if (len > MSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(MessageBuf(), src, len);
    return (Err)len;
}

/**
 * @brief Reads data from the current thread's message buffer.
 *
 * @param dst Pointer to the destination buffer where the data will be read into.
 * @param len Length of the data to read in bytes.
 *
 * @return Err Returns the number of bytes read on success, or a negative error code on failure
 * (e.g., ERR_OVERFLOW if len exceeds MSG_BUF_SIZE).
 */
static inline Err MsgRead(void *dst, size_t len)
{
    if (len > MSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(dst, MessageBuf(), len);
    return len;
}

/* message buffer write cursor */
typedef struct {
    char *buf;    /* == MessageBuf() */
    uint32_t off; /* bytes written so far */
    uint32_t cap; /* MSG_BUF_SIZE */
    bool ovf;     /* set if any append would exceed cap */
} MsgWriter;

static inline void MsgWriterInit(MsgWriter *w)
{
    w->buf = MessageBuf();
    w->off = 0;
    w->cap = MSG_BUF_SIZE;
    w->ovf = false;
}

static inline void MsgPutU32(MsgWriter *w, uint32_t v)
{
    if (w->off + 4 > w->cap) {
        w->ovf = true;
        return;
    }
    memcpy(w->buf + w->off, &v, 4); /* alignment-safe, matches reader */
    w->off += 4;
}

static inline void MsgPutStr(MsgWriter *w, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1; /* include the NUL */
    if (w->off + n > w->cap) {
        w->ovf = true;
        return;
    }
    memcpy(w->buf + w->off, s, n);
    w->off += n;
}

#ifdef __cplusplus
}
#endif
#endif // ZUZU_MSG_H
