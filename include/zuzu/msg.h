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
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_msg_send(handle_t port, uint32_t w1, uint32_t w2, uint32_t w3) {
    return syscall(SYS_MSG_SEND, port, w1, w2, w3);
}

/**
 * @brief Receives a message from the specified port, with an optional timeout.
 * 
 * @param port The handle of the port to receive the message from.
 * @param timeout_ms The timeout in milliseconds to wait for a message. Use TIMEOUT_INFINITE for blocking indefinitely, or TIMEOUT_POLL for non-blocking.
 * @return msg_t Returns 2 of the caller's 3 payload words, r1 is sender's PID.
 */
static inline msg_t zuzu_msg_recv(handle_t port, uint32_t timeout_ms) {
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
 * @return msg_t Returns a msg_t structure containing the reply message. If the call operation fails, the r0 field of the returned msg_t will contain a negative error code.
 */
static inline msg_t zuzu_msg_call(handle_t port, uint32_t w1, uint32_t w2, uint32_t w3) {
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
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_msg_reply(handle_t reply_handle, uint32_t w1, uint32_t w2, uint32_t w3) {
    return syscall(SYS_MSG_REPLY, reply_handle, w1, w2, w3);
}

/**
 * @brief Sends a message with a local buffer to the specified port.
 * 
 * @param port The handle of the port to send the message to.
 * @param buf_len The length of the local buffer to send.
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_msg_lsend(handle_t port, uint32_t buf_len) {
    return syscall(SYS_MSG_LSEND, port, buf_len, 0, 0);
}

/**
 * @brief Performs a local call to the specified port with a local buffer.
 * 
 * @param port The handle of the port to call.
 * @param buf_len The length of the local buffer to send.
 * 
 * @return msg_t Returns a msg_t structure containing the reply message. If the call operation fails, the r0 field of the returned msg_t will contain a negative error code.
 */
static inline msg_t zuzu_msg_lcall(handle_t port, uint32_t buf_len) {
    return syscall_msg(SYS_MSG_LCALL, port, buf_len, 0, 0);
}

/**
 * @brief Replies to a local call with the specified reply handle and local buffer length.
 * 
 * @param reply_handle The handle of the reply to send.
 * @param buf_len The length of the local buffer to send.
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_msg_lreply(handle_t reply_handle, uint32_t buf_len) {
    return syscall(SYS_MSG_LREPLY, reply_handle, buf_len, 0, 0);
}

/**
 * @brief Waits for any message from a set of handles, with an optional timeout.
 * 
 * @param handles Pointer to an array of handles to wait on.
 * @param count The number of handles in the array.
 * @param timeout_ms The timeout in milliseconds to wait for a message. Use TIMEOUT_INFINITE for blocking indefinitely, or TIMEOUT_POLL for non-blocking.
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure. The result of the waitany operation is stored in the provided waitany_result_t structure.
 */
static inline int32_t zuzu_waitany(const handle_t *handles, uint32_t count,
                               uint32_t timeout_ms, waitany_result_t *result)
{
    result->size = sizeof(*result);   /* versioning handshake, owned by the wrapper */

    return syscall(SYS_WAITANY, (uint32_t)(uintptr_t)handles, count, timeout_ms,
                    (uint32_t)(uintptr_t)result);
}

/* ---- Capability syscalls ---- */

/**
 * @brief Creates a new port and returns its handle.
 * 
 * @return int32_t Returns the handle of the newly created port on success, or a negative error code on failure.
 */
static inline int32_t zuzu_port_create(void) {
    return syscall(SYS_PORT_CREATE, 0, 0, 0, 0);
}

/**
 * @brief Grants a capability to the specified process.
 * 
 * @param cap The handle of the capability to grant.
 * @param pid The process ID of the target process to grant the capability to.
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_grant(handle_t cap, zpid_t pid) {
    return syscall(SYS_GRANT, cap, pid, 0, 0);
}

/**
 * @brief Destroys the specified handle, revoking its capability.
 * 
 * @param h The handle to destroy.
 * 
 * @return int32_t Returns 0 on success, or a negative error code on failure.
 */
static inline int32_t zuzu_destroy(handle_t h) {
    return syscall(SYS_DESTROY, h, 0, 0, 0);
}

#ifdef __cplusplus
}
#endif

#endif
