#ifndef ZUZU_CHANNEL_H
#define ZUZU_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

static inline Err ChannelSend(Handle port, const void *buf, size_t len)
{
    if (len > MSG_BUF_SIZE)
        return ERR_BADARG;
    memcpy(MessageBuf(), buf, len);
    return CallPort(port, (uint32_t)len, -1, NULL, NULL);
}

static inline Err ChannelCall(Handle port, const void *buf, size_t len, void *reply,
                              size_t reply_cap)
{
    if (len > MSG_BUF_SIZE)
        return ERR_BADARG;
    memcpy(GetMessageBox(), buf, len);

    uint32_t got = 0;
    Err rc = CallPort(port, (uint32_t)len, -1, &got, NULL);
    if (rc < 0)
        return rc;

    if (got > reply_cap)
        got = (uint32_t)reply_cap;
    if (got && reply)
        memcpy(reply, GetMessageBox(), got);

    return (Err)got;
}

static inline Err ChannelReply(Handle reply_handle, const void *buf, size_t len)
{
    (void)reply_handle;
    if (len > MSG_BUF_SIZE)
        return ERR_OVERFLOW;
    if (len && buf)
        memcpy(GetMessageBox(), buf, len);
    return Reply((uint32_t)len, -1);
}

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_CHANNEL_H */
