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
    return ERR_BUFEMPTY;
}

/* Producer: reserve the next writable slot (or NULL if full). Caller fills
   slot->data and slot->len, then calls packet_ring_commit. */
nic_frame_t *packet_ring_reserve(nic_ring_t *r) {
    if (!r || (r->head + 1) % NIC_RING_DEPTH == r->tail)
        return NULL;
    return &r->slots[r->head];
}

/* Producer: publish the reserved slot. Release barrier so the consumer never
   sees the advanced head before the slot contents. */
void packet_ring_commit(nic_ring_t *r) {
    arch_dmb();
    r->head = (r->head + 1) % NIC_RING_DEPTH;
}

/* Consumer: peek the next readable slot (or NULL if empty). Acquire barrier so
   slot reads are not hoisted above the head observation. */
nic_frame_t *packet_ring_peek(nic_ring_t *r) {
    if (!r || r->head == r->tail)
        return NULL;
    arch_dmb();
    return &r->slots[r->tail];
}

/* Consumer: release the slot after reading it. */
void packet_ring_consume(nic_ring_t *r) {
    arch_dmb();
    r->tail = (r->tail + 1) % NIC_RING_DEPTH;
}
