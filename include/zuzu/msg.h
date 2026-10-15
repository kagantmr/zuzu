#ifndef ZUZU_MSG_H
#define ZUZU_MSG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <util/tls.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <types.h>
#include <zuzu/err.h>

static inline void *GetMessageBox(void) { return ZuzuTLS()->msg_buf; }

static inline Err MsgWrite(const void *src, size_t len)
{
    if (len > MSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(GetMessageBox(), src, len);
    return (Err)len;
}

static inline Err MsgRead(void *dst, size_t len)
{
    if (len > MSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(dst, GetMessageBox(), len);
    return len;
}

typedef struct {
    char *buf;
    uint32_t off;
    uint32_t cap;
    bool ovf;
} MsgWriter;

static inline void MsgWriterInit(MsgWriter *w)
{
    w->buf = GetMessageBox();
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
    memcpy(w->buf + w->off, &v, 4);
    w->off += 4;
}

static inline void MsgPutByte(MsgWriter *w, uint8_t v)
{
    if (w->off + 1 > w->cap) {
        w->ovf = true;
        return;
    }
    *(w->buf + w->off) = v;
    w->off += 1;
}

static inline void MsgPutStr(MsgWriter *w, const char *s)
{
    uint32_t n = (uint32_t)strlen(s) + 1;
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
