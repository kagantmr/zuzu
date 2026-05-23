#ifndef LAN9118_DRV_RING_H
#define LAN9118_DRV_RING_H

#include <stdint.h>
#include <barrier.h>
#include <mem.h>

#define NIC_FRAME_SIZE 1536
#define NIC_RING_DEPTH 4

typedef struct {
    uint16_t len;
    uint8_t  data[NIC_FRAME_SIZE];
} nic_frame_t;

typedef struct {
    nic_frame_t slots[NIC_RING_DEPTH];
    uint32_t    head;
    uint32_t    tail;
} nic_ring_t;

int packet_ring_push(nic_ring_t *r, void *src, uint16_t len);
int packet_ring_pop(nic_frame_t *dst, nic_ring_t *r);

#endif