/*
 * channel.h - high-level bulk IPC
 *
 * Wraps the message buffer + Call/Reply svc verbs into a clean
 * three-function API. Callers never touch the message buffer directly.
 *
 * Sender side:
 *   ChannelSend(port, buf, len)
 *   ChannelCall(port, buf, len, reply, reply_len)
 *
 * Server side:
 *   ChannelReply(reply_handle, buf, len) reply to the caller currently
 *   blocked in Call() on us -- see kernel/svc/svc_reply.c: which caller
 *   that is is tracked implicitly by the kernel (current_task->reply_cap),
 *   not by reply_handle. reply_handle is kept only for source
 *   compatibility with callers that still pass one.
 */

#ifndef ZUZU_CHANNEL_H
#define ZUZU_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <string.h>
#include <util/msg.h>
#include <zuzu/err.h>
#include <zuzu/zuzu.h>

/**
 * @brief Sends a message to the specified port and waits for a reply.
 * Prowl's IPC ABI has no one-way send verb -- every Call() blocks until the
 * receiver Replies -- so despite the name this does not return until the
 * other side replies. Kept for callers that only care about delivering the
 * payload and ignore the (discarded) reply.
 *
 * @param port The handle of the port to send the message to.
 * @param buf Pointer to the buffer containing the message data to send.
 * @param len The length of the message data in bytes.
 *
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ChannelSend(Handle port, const void *buf, size_t len)
{
    if (len > MSG_BUF_SIZE)
        return ERR_BADARG;
    memcpy(MessageBuf(), buf, len);
    SvcResult r = Call(port, (uint32_t)len, -1);
    return (Err)r.r0;
}

/**
 * @brief Sends a message to the specified port and waits for a reply.
 *
 * @param port The handle of the port to send the message to.
 * @param buf Pointer to the buffer containing the message data to send.
 * @param len The length of the message data in bytes.
 * @param reply Pointer to the buffer that will receive the reply data.
 * @param reply_cap The maximum length of the reply buffer in bytes.
 *
 * @return Err Returns the number of bytes received in the reply on success, or a negative error
 * code on failure.
 */
static inline Err ChannelCall(Handle port, const void *buf, size_t len, void *reply,
                              size_t reply_cap)
{
    if (len > MSG_BUF_SIZE)
        return ERR_BADARG;
    memcpy(MessageBuf(), buf, len);

    SvcResult r = Call(port, (uint32_t)len, -1);
    if ((Err)r.r0 < 0)
        return (Err)r.r0;

    uint32_t got = (uint32_t)r.r1;
    if (got > reply_cap)
        got = (uint32_t)reply_cap;
    if (got && reply)
        memcpy(reply, MessageBuf(), got);

    return (Err)got;
}

/**
 * @brief Replies to a call with the specified reply data.
 *
 * @param reply_handle Unused; replies target the current call context implicitly.
 * @param buf Pointer to the buffer containing the reply data to send.
 * @param len The length of the reply data in bytes.
 *
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ChannelReply(Handle reply_handle, const void *buf, size_t len)
{
    (void)reply_handle;
    if (len > MSG_BUF_SIZE)
        return ERR_OVERFLOW;
    if (len && buf)
        memcpy(MessageBuf(), buf, len);
    return Reply((uint32_t)len, -1);
}

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_CHANNEL_H */
