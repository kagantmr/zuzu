#include <zuzu/packetring.h>

int packet_ring_push(nic_ring_t *r, void *src, uint16_t len) {
    if (!((r->head + 1) % NIC_RING_DEPTH == r->tail)) { 
        r->slots[r->head].len = len;
        memcpy(r->slots[r->head].data, src, len);
        arch_dmb();
        r->head = (r->head+1) % NIC_RING_DEPTH;
        return 0;
    }
    return -1;
}

int packet_ring_pop(nic_frame_t *dst, nic_ring_t *r) {
    if (r->head != r->tail) {
        dst->len = r->slots[r->tail].len;
        memcpy(dst->data, r->slots[r->tail].data, r->slots[r->tail].len);
        arch_dmb();
        r->tail = (r->tail+1) % NIC_RING_DEPTH;
        return 0;
    }
    return -1;
}