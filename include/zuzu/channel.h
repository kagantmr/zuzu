/*
 * channel.h - high-level bulk IPC
 *
 * Wraps the raw lmsg buffer + lsend/lcall/lreply syscalls into a clean
 * three-function API. Callers never touch the lmsg buffer directly.
 *
 * Sender side:
 *   ChannelSend(port, buf, len)          
 *   ChannelCall(port, buf, len,          
 *             reply, reply_len)
 *
 * Server side:
 *   chan_reply(reply_handle, buf, len) reply to a ZuzuMsgLcall caller
 */


#ifndef ZUZU_CHANNEL_H
#define ZUZU_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

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
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline Err ChannelSend(Handle port, const void *buf, size_t len)
{
    if (len > LMSG_BUF_SIZE) return ERR_BADARG;
    memcpy(LmsgBuf(), buf, len);
    return ZuzuMsgLsend(port, len);
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
 * @return Err Returns the number of bytes received in the reply on success, or a negative error code on failure.
 */
static inline Err ChannelCall(Handle port,
                                const void *buf,    size_t len,
                                void       *reply,  size_t reply_cap)
{
    if (len > LMSG_BUF_SIZE) return ERR_BADARG;
    memcpy(LmsgBuf(), buf, len);

    Message msg = ZuzuMsgLcall(port, len);
    if (msg.w0 < 0)
        return msg.w0;

    uint32_t got = msg.w1;
    if (got > reply_cap) got = reply_cap;
    if (got && reply)
        memcpy(reply, LmsgBuf(), got);

    return (Err)got;
}

/**
 * @brief Replies to a long message call with the specified reply data. Needn't call lmsg functions.
 * 
 * @param reply_handle The handle received from a ZuzuMsgLcall that is being replied to.
 * @param buf Pointer to the buffer containing the reply data to send.
 * @param len The length of the reply data in bytes.
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ChannelReply(Handle reply_handle,
                                 const void *buf, size_t len)
{
    if (len > LMSG_BUF_SIZE) return ERR_OVERFLOW;
    if (len && buf)
        memcpy(LmsgBuf(), buf, len);
    return ZuzuMsgLreply(reply_handle, len);
}

#ifdef __cplusplus
}
#endif

#endif /* ZUZU_CHANNEL_H */