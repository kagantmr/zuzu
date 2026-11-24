/**
 * @file ctl.h
 *
 * @brief The ctl thread: receives calls on /svc/netd and hands them to the tcpip thread.
 */

#ifndef NETD_CTL_H
#define NETD_CTL_H

#include <stdbool.h>
#include <types.h>
#include <zuzu/err.h>

/** @brief Bit on g_event the ctl thread signals after queueing a call. */
#define CTL_MBOX_BIT 0u
/** @brief Mask of CTL_MBOX_BIT. */
#define CTL_MBOX_MASK (1u << CTL_MBOX_BIT)

/**
 * @brief Starts the ctl thread. Call after the startup driver handshake.
 * @return ZUZU_OK, or the error that kept the thread from starting.
 */
Err CtlStart(void);

/**
 * @brief tcpip thread: takes the next queued call and makes it current.
 *
 * The request is copied into the calling thread's message box, so handlers read it with
 * GetMessageBox() as before.
 *
 * @param[out] call Receives the call's sender, length and granted handle.
 * @return true if a call was taken, false if the queue is empty.
 */
bool CtlNextCall(PortWaitResult *call);

/**
 * @brief tcpip thread: answers the current call with the first @p xlen bytes of the message box.
 * @param[in] xlen  Reply length in bytes.
 * @param[in] grant Handle to pass to the caller, or -1.
 * @return ZUZU_OK, or the error from ReplyTo().
 */
Err CtlReply(uint32_t xlen, Handle grant);

#endif /* NETD_CTL_H */
