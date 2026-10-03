#ifndef LAN9118_DRV_RING_H
#define LAN9118_DRV_RING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <arch/barrier.h>
#include <string.h>

#define NIC_FRAME_SIZE 1536
#define NIC_RING_DEPTH 16
#define NIC_MTU 1500

typedef struct
{
    uint32_t len; // length of the data in the frame (in bytes)
    uint8_t data[NIC_FRAME_SIZE]; // data buffer for the frame
} NicFrame;

typedef struct
{
    NicFrame slots[NIC_RING_DEPTH]; // array of slots in the ring buffer
    volatile uint32_t head; // index of the next slot to be written by the producer
    volatile uint32_t tail; // index of the next slot to be read by the consumer
} NicRing;

#define NIC_RING_BYTES sizeof(NicRing)                         /* 24648 */
#define NIC_RING_STRIDE (((NIC_RING_BYTES + 4095) / 4096) * 4096) /* 28672 */
#define NIC_RX_OFFSET 0u
#define NIC_TX_OFFSET NIC_RING_STRIDE
#define NIC_SHM_BYTES (2u * NIC_RING_STRIDE) /* 57344 = 14 pages */

_Static_assert(NIC_TX_OFFSET >= NIC_RING_BYTES, "rings overlap");
_Static_assert(NIC_SHM_BYTES >= NIC_TX_OFFSET + NIC_RING_BYTES, "shm too small");

/**
 * @brief Pushes a packet into the NIC ring buffer.
 * 
 * @param r Pointer to the NIC ring buffer.
 * @param src Pointer to the source data to be pushed into the ring buffer.
 * @param len Length of the data to be pushed into the ring buffer (in bytes).
 * 
 * @return int Returns 0 on success, otherwise ERR_BUFFULL or ERR_BUFEMPTY.
 */
int PacketRingPush(NicRing *r, void *src, uint16_t len);
int PacketRingPop(NicFrame *dst, NicRing *r);

/**
 * @brief Reserves a slot in the NIC ring buffer for writing a packet.
 * 
 * @param r Pointer to the NIC ring buffer.
 * @return nic_frame_t* Returns a pointer to the reserved slot in the ring buffer, or NULL if the buffer is full.
 * 
 */
NicFrame *PacketRingReserve(NicRing *r);
void         PacketRingCommit(NicRing *r);
NicFrame *PacketRingPeek(NicRing *r);
void         PacketRingConsume(NicRing *r);

#ifdef __cplusplus
}
#endif

#endif