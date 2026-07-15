#ifndef ZUZU_CHANNEL_H
#define ZUZU_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * @brief Sends a long message to the specified port. Needn't call lmsg functions.
 * 
 * @param port The handle of the port to send the lmessage to.
 * @param buf Pointer to the buffer containing the lmessage data to send.
 * @param len The length of the message data in bytes.
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t chan_send(handle_t port, const void *buf, uint32_t len)
{
    if (len > LMSG_BUF_SIZE) return ERR_BADARG;
    memcpy(lmsg_buf(), buf, len);
    return zuzu_msg_lsend(port, len);
}

/**
 * @brief Sends a long message to the specified port and waits for a reply. Needn't call lmsg functions.
 * 
 * @param port The handle of the port to send the lmessage to.
 * @param buf Pointer to the buffer containing the lmessage data to send.
 * @param len The length of the message data in bytes.
 * @param reply Pointer to the buffer that will receive the reply data.
 * @param reply_cap The maximum length of the reply buffer in bytes.
 * 
 * @return int32_t Returns the number of bytes received in the reply on success, or a negative error code on failure.
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

/**
 * @brief Replies to a long message call with the specified reply data. Needn't call lmsg functions.
 * 
 * @param reply_handle The handle received from a zuzu_msg_lcall that is being replied to.
 * @param buf Pointer to the buffer containing the reply data to send.
 * @param len The length of the reply data in bytes.
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t chan_reply(handle_t reply_handle,
                                 const void *buf, uint32_t len)
{
    if (len > LMSG_BUF_SIZE) return ERR_BADARG;
    if (len && buf)
        memcpy(lmsg_buf(), buf, len);
    return zuzu_msg_lreply(reply_handle, len);
}

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_CHANNEL_H */