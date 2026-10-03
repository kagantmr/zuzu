#include <net/packetring.h>
#include <zuzu/err.h>

int PacketRingPush(NicRing *r, void *src, uint16_t len) {
    if (!r || !src || len > NIC_FRAME_SIZE)
        return ERR_OVERFLOW;

    if (!((r->head + 1) % NIC_RING_DEPTH == r->tail)) {
        r->slots[r->head].len = len;
        memcpy(r->slots[r->head].data, src, len);
        ArchDmb();
        r->head = (r->head+1) % NIC_RING_DEPTH;
        return 0;
    }
    return ERR_BUFFULL;
}

int PacketRingPop(NicFrame *dst, NicRing *r) {
    if (!dst || !r)
        return ERR_BADARG;

    if (r->head != r->tail) {
        ArchDmb();
        uint16_t len = r->slots[r->tail].len;
        if (len > NIC_FRAME_SIZE) {
            r->tail = (r->tail+1) % NIC_RING_DEPTH;
            return ERR_OVERFLOW;
        }

        dst->len = len;
        memcpy(dst->data, r->slots[r->tail].data, len);
        ArchDmb();
        r->tail = (r->tail+1) % NIC_RING_DEPTH;
        return 0;
    }
    return ERR_BUFEMPTY;
}

/* Producer: reserve the next writable slot (or NULL if full). Caller fills
   slot->data and slot->len, then calls packet_ring_commit. */
NicFrame *PacketRingReserve(NicRing *r) {
    if (!r || (r->head + 1) % NIC_RING_DEPTH == r->tail)
        return NULL;
    return &r->slots[r->head];
}

/* Producer: publish the reserved slot. Release barrier so the consumer never
   sees the advanced head before the slot contents. */
void PacketRingCommit(NicRing *r) {
    ArchDmb();
    r->head = (r->head + 1) % NIC_RING_DEPTH;
}

/* Consumer: peek the next readable slot (or NULL if empty). Acquire barrier so
   slot reads are not hoisted above the head observation. */
NicFrame *PacketRingPeek(NicRing *r) {
    if (!r || r->head == r->tail)
        return NULL;
    ArchDmb();
    return &r->slots[r->tail];
}

/* Consumer: release the slot after reading it. */
void PacketRingConsume(NicRing *r) {
    ArchDmb();
    r->tail = (r->tail + 1) % NIC_RING_DEPTH;
}
