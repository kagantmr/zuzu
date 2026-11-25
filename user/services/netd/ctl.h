/**
 * @file ctl.h
 *
 * @brief The ctl thread: receives calls on /svc/netd and hands them to the tcpip thread.
 */

#ifndef NETD_CTL_H
#define NETD_CTL_H

#include "common/netif.h"
#include "common/netmq.h"
#include <stdbool.h>
#include <types.h>
#include <zuzu/err.h>

/** @brief Bit on g_event the ctl thread signals after queueing a call. */
#define CTL_MBOX_BIT 0u
/** @brief Mask of CTL_MBOX_BIT. */
#define CTL_MBOX_MASK (1u << CTL_MBOX_BIT)

/** @brief Internal opcode: the ctl thread finished a driver handshake. Refused from apps. */
#define CTL_OP_ATTACH 0xF0u

/** @brief Body of a CTL_OP_ATTACH message: a driver's rings, mapped by the ctl thread. */
typedef struct {
    uint8_t op;                /**< CTL_OP_ATTACH. */
    uint8_t slot;              /**< netifs[] index to attach. */
    uint8_t reattach;          /**< Nonzero if the slot had a driver before (driver restart). */
    uint8_t _pad;              /**< Keeps the fields below aligned. */
    MacAddr mac;               /**< Hardware address the driver reported. */
    char name[NETIF_NAME_MAX]; /**< Name the ctl thread gave the interface. */
    NicRing *tx_ring;          /**< Mapped tx ring. */
    NicRing *rx_ring;          /**< Mapped rx ring. */
    Handle doorbell;           /**< Driver's event handle. */
} CtlAttach;

_Static_assert(sizeof(CtlAttach) <= NETMQ_BODY_MAX, "CtlAttach must fit a NetdMsg body");

/**
 * @brief Starts the ctl thread, which also runs every driver handshake.
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
