#ifndef ZUZU_CHANNEL_H
#define ZUZU_CHANNEL_H

/*
 * channel.h - high-level bulk IPC
 *
 * Wraps the raw ipcx buffer + sendx/callx/replyx syscalls into a clean
 * three-function API. Callers never touch IPCX_BUF directly.
 *
 * Sender side:
 *   chan_send(port, buf, len)          
 *   chan_call(port, buf, len,          
 *             reply, reply_len)
 *
 * Server side:
 *   chan_reply(reply_handle, buf, len) reply to a _lcall caller
 */

#include <zuzu/ipc.h>
#include <zuzu/ipcx.h>
#include <string.h>
#include <stdint.h>

/*
 * copy buf into the ipcx buffer and send one-way.
 * Returns 0 on success, negative error on failure.
 */
static inline int32_t chan_send(handle_t port, const void *buf, uint32_t len)
{
    if (len > IPCX_BUF_SIZE) len = IPCX_BUF_SIZE;
    memcpy(ipcx_buf(), buf, len);
    return _lsend(port, len);
}

/*
 * Returns the number of reply bytes written into reply_buf, or a negative
 * error code if the call failed.
 */
static inline int32_t chan_call(handle_t port,
                                const void *buf,    uint32_t len,
                                void       *reply,  uint32_t reply_cap)
{
    if (len > IPCX_BUF_SIZE) len = IPCX_BUF_SIZE;
    memcpy(ipcx_buf(), buf, len);

    msg_t msg = _lcall(port, len);
    if ((int32_t)msg.r0 < 0)
        return (int32_t)msg.r0;

    uint32_t got = msg.r1;
    if (got > reply_cap) got = reply_cap;
    if (got && reply)
        memcpy(reply, ipcx_buf(), got);

    return (int32_t)got;
}

/*
 * Returns 0 on success, negative error on failure.
 */
static inline int32_t chan_reply(handle_t reply_handle,
                                 const void *buf, uint32_t len)
{
    if (len > IPCX_BUF_SIZE) len = IPCX_BUF_SIZE;
    if (len && buf)
        memcpy(ipcx_buf(), buf, len);
    return _lreply(reply_handle, len);
}

#endif /* ZUZU_CHANNEL_H */