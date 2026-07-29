#ifndef ZUZU_MSG_H
#define ZUZU_MSG_H

#include "zuzu/syscall_nums.h"
#include "zuzu/types.h"
#include <arch/syscall.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- IPC syscalls ---- */

/**
 * @brief Sends a message to the specified port with the given payload.
 * 
 * @param port The handle of the port to send the message to.
 * @param w1 The first word of the message payload.
 * @param w2 The second word of the message payload.
 * @param w3 The third word of the message payload.
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuMsgSend(Handle port, MsgWord w1, MsgWord w2, MsgWord w3) {
    return Syscall(SYS_MSG_SEND, port, w1, w2, w3);
}

/**
 * @brief Receives a message from the specified port, with an optional timeout.
 * 
 * @param port The handle of the port to receive the message from.
 * @param timeout_ms The timeout in milliseconds to wait for a message. Use TIMEOUT_INFINITE for blocking indefinitely, or TIMEOUT_POLL for non-blocking.
 * 
 * @return Message Returns 2 of the caller's 3 payload words, r1 is sender's PID.
 */
static inline Message ZuzuMsgRecv(Handle port, Duration timeout_ms) {
    return syscall_msg(SYS_MSG_RECV, port, timeout_ms, 0, 0);
}

/**
 * @brief Sends a message to the specified port and waits for a reply, with the given payload.
 * 
 * @param port The handle of the port to send the message to.
 * @param w1 The first word of the message payload.
 * @param w2 The second word of the message payload.
 * @param w3 The third word of the message payload.
 * 
 * @return Message Returns a Message structure containing the reply message. If the call operation fails, the r0 field of the returned Message will contain a negative error code.
 */
static inline Message ZuzuMsgCall(Handle port, MsgWord w1, MsgWord w2, MsgWord w3) {
    return syscall_msg(SYS_MSG_CALL, port, w1, w2, w3);
}

/**
 * @brief Replies to a message with the specified reply handle and payload.
 * 
 * @param reply_handle The handle of the reply to send.
 * @param w1 The first word of the reply payload.
 * @param w2 The second word of the reply payload.
 * @param w3 The third word of the reply payload. (dropped)
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuMsgReply(Handle reply_handle, MsgWord w1, MsgWord w2, MsgWord w3) {
    return Syscall(SYS_MSG_REPLY, reply_handle, w1, w2, w3);
}

/**
 * @brief Sends a message with a local buffer to the specified port.
 * 
 * @param port The handle of the port to send the message to.
 * @param buf_len The length of the local buffer to send.
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuMsgLsend(Handle port, size_t buf_len) {
    return Syscall(SYS_MSG_LSEND, port, buf_len, 0, 0);
}

/**
 * @brief Performs a local call to the specified port with a local buffer.
 * 
 * @param port The handle of the port to call.
 * @param buf_len The length of the local buffer to send.
 * 
 * @return Message Returns a Message structure containing the reply message. If the call operation fails, the r0 field of the returned Message will contain a negative error code.
 */
static inline Message ZuzuMsgLcall(Handle port, size_t buf_len) {
    return syscall_msg(SYS_MSG_LCALL, port, buf_len, 0, 0);
}

/**
 * @brief Replies to a local call with the specified reply handle and local buffer length.
 * 
 * @param reply_handle The handle of the reply to send.
 * @param buf_len The length of the local buffer to send.
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuMsgLreply(Handle reply_handle, size_t buf_len) {
    return Syscall(SYS_MSG_LREPLY, reply_handle, buf_len, 0, 0);
}

/**
 * @brief Waits for any message from a set of handles, with an optional timeout.
 * 
 * @param handles Pointer to an array of handles to wait on.
 * @param count The number of handles in the array.
 * @param timeout_ms The timeout in milliseconds to wait for a message. Use TIMEOUT_INFINITE for blocking indefinitely, or TIMEOUT_POLL for non-blocking.
 * 
 * @return Err Returns 0 on success, or a negative error code on failure. The result of the waitany operation is stored in the provided WaitanyResult structure.
 */
static inline Err ZuzuWaitany(const Handle *handles, size_t count,
                               Duration timeout_ms, WaitanyResult *result)
{
    result->size = sizeof(*result);   /* versioning handshake, owned by the wrapper */

    return Syscall(SYS_WAITANY, (uint32_t)(VirtAddr)handles, count, timeout_ms,
                    (uint32_t)(VirtAddr)result);
}

/* ---- Capability syscalls ---- */

/**
 * @brief Creates a new port and returns its handle.
 * 
 * @return Err Returns the handle of the newly created port on success, or a negative error code on failure.
 */
static inline Err ZuzuPortCreate(void) {
    return Syscall(SYS_PORT_CREATE, 0, 0, 0, 0);
}

/**
 * @brief Grants a capability to the specified process.
 * 
 * @param cap The handle of the capability to grant.
 * @param pid The process ID of the target process to grant the capability to.
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuGrant(Handle cap, Pid pid) {
    return Syscall(SYS_GRANT, cap, pid, 0, 0);
}

/**
 * @brief Destroys the specified handle, revoking its capability.
 * 
 * @param h The handle to destroy.
 * 
 * @return Err Returns 0 on success, or a negative error code on failure.
 */
static inline Err ZuzuDestroy(Handle h) {
    return Syscall(SYS_DESTROY, h, 0, 0, 0);
}

#ifdef __cplusplus
}
#endif

#endif
