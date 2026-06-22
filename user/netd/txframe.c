#include "txframe.h"
#include <zuzu/err.h>

int txframe_init(txframe_t *f, uint16_t headroom) {
    nic_frame_t *slot = packet_ring_reserve(tx_ring);
    if (!slot)
        return ERR_BUFFULL;
    f->slot = slot;
    f->front = headroom;
    f->end = headroom;
    return ZUZU_OK;
}

void *txframe_append(txframe_t *f, uint16_t n) {
    if ((size_t)f->end + n > NIC_FRAME_SIZE)
        return NULL;
    uint8_t *p = f->slot->data + f->end;
    f->end += n;
    return p;
}

void *txframe_prepend(txframe_t *f, uint16_t n) {
    if (n > f->front)
        return NULL;
    f->front -= n;
    return f->slot->data + f->front;
}
