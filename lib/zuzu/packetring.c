#include <zuzu/packetring.h>
#include <zuzu/err.h>

int packet_ring_push(nic_ring_t *r, void *src, uint16_t len) {
    if (!r || !src || len > NIC_FRAME_SIZE)
        return ERR_OVERFLOW;

    if (!((r->head + 1) % NIC_RING_DEPTH == r->tail)) { 
        r->slots[r->head].len = len;
        memcpy(r->slots[r->head].data, src, len);
        arch_dmb();
        r->head = (r->head+1) % NIC_RING_DEPTH;
        return 0;
    }
    return ERR_BUFFULL;
}

int packet_ring_pop(nic_frame_t *dst, nic_ring_t *r) {
    if (!dst || !r)
        return ERR_BADARG;

    if (r->head != r->tail) {
        arch_dmb();
        uint16_t len = r->slots[r->tail].len;
        if (len > NIC_FRAME_SIZE) {
            r->tail = (r->tail+1) % NIC_RING_DEPTH;
            return ERR_OVERFLOW;
        }

        dst->len = len;
        memcpy(dst->data, r->slots[r->tail].data, len);
        arch_dmb();
        r->tail = (r->tail+1) % NIC_RING_DEPTH;
        return 0;
    }
    return ERR_NOENT;
}
