#ifndef ZUZU_CHANNEL_H
#define ZUZU_CHANNEL_H

/*
 * channel.h - high-level bulk IPC
 *
 * Wraps the raw lmsg buffer + lsend/lcall/lreply syscalls into a clean
 * three-function API. Callers never touch the lmsg buffer directly.
 *
 * Sender side:
 *   chan_send(port, buf, len)          
 *   chan_call(port, buf, len,          
 *             reply, reply_len)
 *
 * Server side:
 *   chan_reply(reply_handle, buf, len) reply to a zuzu_msg_lcall caller
 */

#include <zuzu/msg.h>
#include <zuzu/lmsg.h>
#include <zuzu/err.h>
#include <string.h>
#include <stdint.h>

/*
 * copy buf into the lmsg buffer and send one-way.
 * Returns 0 on success, negative error on failure.
 * Payloads over LMSG_BUF_SIZE are rejected, never truncated.
 */
static inline int32_t chan_send(handle_t port, const void *buf, uint32_t len)
{
    if (len > LMSG_BUF_SIZE) return ERR_BADARG;
    memcpy(lmsg_buf(), buf, len);
    return zuzu_msg_lsend(port, len);
}

/*
 * Returns the number of reply bytes written into reply_buf, or a negative
 * error code if the call failed.
 */
static inline int32_t chan_call(handle_t port,
                                const void *buf,    uint32_t len,
                                void       *reply,  uint32_t reply_cap)
{
    if (len > LMSG_BUF_SIZE) return ERR_BADARG;
    memcpy(lmsg_buf(), buf, len);

    msg_t msg = zuzu_msg_lcall(port, len);
    if ((int32_t)msg.r0 < 0)
        return (int32_t)msg.r0;

    uint32_t got = msg.r1;
    if (got > reply_cap) got = reply_cap;
    if (got && reply)
        memcpy(reply, lmsg_buf(), got);

    return (int32_t)got;
}

/*
 * Returns 0 on success, negative error on failure.
 */
static inline int32_t chan_reply(handle_t reply_handle,
                                 const void *buf, uint32_t len)
{
    if (len > LMSG_BUF_SIZE) return ERR_BADARG;
    if (len && buf)
        memcpy(lmsg_buf(), buf, len);
    return zuzu_msg_lreply(reply_handle, len);
}

#endif /* ZUZU_CHANNEL_H */