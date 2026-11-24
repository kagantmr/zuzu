/**
 * @file netmq.h
 *
 * @brief netD's message queue architecture.
 */

#ifndef NETD_NETMQ_H
#define NETD_NETMQ_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <zuzu/err.h>

/** @brief Number of slots in the queue; a power of two. */
#define NETMQ_DEPTH 64u
/** @brief Largest request body a message carries. */
#define NETMQ_BODY_MAX 128u

_Static_assert((NETMQ_DEPTH & (NETMQ_DEPTH - 1)) == 0, "NETMQ_DEPTH must be a power of two");

/** @brief A parked call handed from the ctl thread to the tcpip thread. */
typedef struct {
    uint8_t op;                /**< Opcode of the call. */
    Ticket ticket;             /**< Ticket from Hold(), answered with ReplyTo(). */
    Marker sender;             /**< Marker of the calling task. */
    Handle granted;            /**< Handle granted with the call, or -1. */
    uint16_t len;              /**< Valid bytes in body. */
    char body[NETMQ_BODY_MAX]; /**< Request bytes copied from the message box. */
} NetdMsg;

/** @brief Single-producer, single-consumer ring of NetdMsg. */
typedef struct {
    NetdMsg queue[NETMQ_DEPTH]; /**< Message slots. */
    volatile size_t head;       /**< Messages pushed so far; written only by the producer. */
    volatile size_t tail;       /**< Messages popped so far; written only by the consumer. */
} NetdMessageQueue;

/**
 * @brief Empties @p q.
 * @param[out] q Queue.
 */
void NetMqInit(NetdMessageQueue *q);

/**
 * @brief Producer: copies @p msg into @p q.
 * @param[in,out] q   Queue.
 * @param[in]     msg Message to copy.
 * @return ZUZU_OK, or ERR_BUSY if the queue is full.
 */
Err NetMqPush(NetdMessageQueue *q, const NetdMsg *msg);

/**
 * @brief Consumer: copies the oldest message of @p q into @p out and removes it.
 * @param[in,out] q   Queue.
 * @param[out]    out Receives the message.
 * @return ZUZU_OK, or ERR_NOENT if the queue is empty.
 */
Err NetMqPop(NetdMessageQueue *q, NetdMsg *out);

#endif /* NETD_NETMQ_H */
