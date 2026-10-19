/**
 * @file
 * @brief Single-producer, single-consumer ring of Ethernet frames between a NIC driver and netd.
 */

#ifndef LAN9118_DRV_RING_H
#define LAN9118_DRV_RING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/barrier.h>
#include <stdint.h>
#include <string.h>

/** @brief Bytes per frame slot. */
#define NIC_FRAME_SIZE 1536
/** @brief Slots per ring; one stays empty, so a ring holds NIC_RING_DEPTH - 1 frames. */
#define NIC_RING_DEPTH 16
/** @brief Largest IP packet in a frame. */
#define NIC_MTU 1500

/** @brief One frame slot. */
typedef struct {
    uint32_t len;                 /**< Bytes of @c data in use. */
    uint8_t data[NIC_FRAME_SIZE]; /**< Frame bytes. */
} NicFrame;

/** @brief A ring of frames in shared memory; empty when head == tail. */
typedef struct {
    NicFrame slots[NIC_RING_DEPTH]; /**< Frame slots. */
    volatile uint32_t head;         /**< Next slot the producer writes. */
    volatile uint32_t tail;         /**< Next slot the consumer reads. */
} NicRing;

/** @brief Size of one ring (24648 bytes). */
#define NIC_RING_BYTES sizeof(NicRing)
/** @brief Ring size rounded up to whole pages (28672 bytes). */
#define NIC_RING_STRIDE (((NIC_RING_BYTES + 4095) / 4096) * 4096)
/** @brief Offset of the rx ring in the shared memory. */
#define NIC_RX_OFFSET 0u
/** @brief Offset of the tx ring in the shared memory. */
#define NIC_TX_OFFSET NIC_RING_STRIDE
/** @brief Size of the shared memory: 14 pages. */
#define NIC_SHM_BYTES (2u * NIC_RING_STRIDE)

_Static_assert(NIC_TX_OFFSET >= NIC_RING_BYTES, "rings overlap");
_Static_assert(NIC_SHM_BYTES >= NIC_TX_OFFSET + NIC_RING_BYTES, "shm too small");

/**
 * @brief Producer: copies a frame into the ring.
 *
 * @param[in,out] r   Ring.
 * @param[in]     src Frame bytes.
 * @param[in]     len Frame length, at most NIC_FRAME_SIZE.
 * @return 0, or an error code.
 * @retval ERR_OVERFLOW @p r or @p src is NULL, or @p len is too large.
 * @retval ERR_BUFFULL  The ring is full.
 */
int PacketRingPush(NicRing *r, void *src, uint16_t len);

/**
 * @brief Consumer: copies the oldest frame out of the ring.
 *
 * @param[out]    dst Where to copy the frame.
 * @param[in,out] r   Ring.
 * @return 0, or an error code.
 * @retval ERR_BADARG   @p dst or @p r is NULL.
 * @retval ERR_BUFEMPTY The ring is empty.
 * @retval ERR_OVERFLOW The slot claimed an impossible length; it has been skipped.
 */
int PacketRingPop(NicFrame *dst, NicRing *r);

/**
 * @brief Producer: returns the next free slot to fill in place, without copying.
 *
 * Fill in its data and len, then call PacketRingCommit().
 *
 * @param[in] r Ring.
 * @return The slot, or NULL if the ring is full or @p r is NULL.
 */
NicFrame *PacketRingReserve(NicRing *r);

/**
 * @brief Producer: publishes the slot from PacketRingReserve().
 *
 * A barrier orders the slot's contents before the new head.
 *
 * @param[in,out] r Ring.
 */
void PacketRingCommit(NicRing *r);

/**
 * @brief Consumer: returns the oldest frame to read in place, without copying.
 *
 * Call PacketRingConsume() when done with it.
 *
 * @param[in] r Ring.
 * @return The slot, or NULL if the ring is empty or @p r is NULL.
 */
NicFrame *PacketRingPeek(NicRing *r);

/**
 * @brief Consumer: releases the slot from PacketRingPeek() back to the producer.
 *
 * @param[in,out] r Ring.
 */
void PacketRingConsume(NicRing *r);

#ifdef __cplusplus
}
#endif

#endif