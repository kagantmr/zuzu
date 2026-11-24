#ifndef NETD_TXFRAME_H
#define NETD_TXFRAME_H

#include "globals.h"
#include "netif.h"
#include <net/packetring.h>
#include <types.h>

typedef struct {
    Netif *nif;     /* interface whose tx ring holds slot */
    NicFrame *slot; /* reserved tx-ring slot (head not yet advanced) */
    uint16_t front;    /* offset of the current outermost byte          */
    uint16_t end;      /* offset one past the last byte                 */
} TxFrame;

/* Reserve a slot in nif's tx ring leaving "headroom" bytes in front of the payload.
   Returns ZUZU_OK, or ERR_BUFFULL when the ring is full. */
int TxFrameInit(TxFrame *f, Netif *nif, uint16_t headroom);

/* Append "n" payload bytes after the current content; returns a pointer to the
   region to fill, or NULL on overflow. */
void *TxFrameAppend(TxFrame *f, uint16_t n);

/* Prepend "n" header bytes in front of the current content; returns a pointer
   to the region to fill, or NULL when the reserved headroom is exhausted. */
void *TxFramePrepend(TxFrame *f, uint16_t n);

/* Start and length of the bytes built so far. */
static inline uint8_t *TxFrameData(TxFrame *f) { return f->slot->data + f->front; }
static inline uint16_t TxFrameLen(TxFrame *f) { return (uint16_t)(f->end - f->front); }

#endif /* NETD_TXFRAME_H */
