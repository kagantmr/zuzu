#ifndef LAN9118_DRV_RING_H
#define LAN9118_DRV_RING_H

#include <stdint.h>
#include <barrier.h>
#include <mem.h>

#define NIC_FRAME_SIZE 1536
#define NIC_RING_DEPTH 16
#define NIC_MTU 1500

typedef struct
{
    uint32_t len;
    uint8_t data[NIC_FRAME_SIZE];
} nic_frame_t;

typedef struct
{
    nic_frame_t slots[NIC_RING_DEPTH];
    volatile uint32_t head;
    volatile uint32_t tail;
} nic_ring_t;

#define NIC_RING_BYTES sizeof(nic_ring_t)                         /* 24648 */
#define NIC_RING_STRIDE (((NIC_RING_BYTES + 4095) / 4096) * 4096) /* 28672 */
#define NIC_RX_OFFSET 0u
#define NIC_TX_OFFSET NIC_RING_STRIDE
#define NIC_SHM_BYTES (2u * NIC_RING_STRIDE) /* 57344 = 14 pages */

_Static_assert(NIC_TX_OFFSET >= NIC_RING_BYTES, "rings overlap");
_Static_assert(NIC_SHM_BYTES >= NIC_TX_OFFSET + NIC_RING_BYTES, "shm too small");

int packet_ring_push(nic_ring_t *r, void *src, uint16_t len);
int packet_ring_pop(nic_frame_t *dst, nic_ring_t *r);

#endif