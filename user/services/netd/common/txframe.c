#include "txframe.h"
#include <zuzu/err.h>

int TxFrameInit(TxFrame *f, uint16_t headroom)
{
    NicFrame *slot = PacketRingReserve(tx_ring);
    if (!slot)
        return ERR_BUFFULL;
    f->slot = slot;
    f->front = headroom;
    f->end = headroom;
    return ZUZU_OK;
}

void *TxFrameAppend(TxFrame *f, uint16_t n)
{
    if ((size_t)f->end + n > NIC_FRAME_SIZE)
        return NULL;
    uint8_t *p = f->slot->data + f->end;
    f->end += n;
    return p;
}

void *TxFramePrepend(TxFrame *f, uint16_t n)
{
    if (n > f->front)
        return NULL;
    f->front -= n;
    return f->slot->data + f->front;
}
