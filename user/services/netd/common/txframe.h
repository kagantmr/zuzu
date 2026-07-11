#ifndef NETD_TXFRAME_H
#define NETD_TXFRAME_H

#include <zuzu/types.h>
#include <zuzu/packetring.h>
#include "globals.h"

typedef struct {
    nic_frame_t *slot;  /* reserved tx-ring slot (head not yet advanced) */
    uint16_t front;     /* offset of the current outermost byte          */
    uint16_t end;       /* offset one past the last byte                 */
} txframe_t;

/* Reserve a tx-ring slot leaving "headroom" bytes in front of the payload.
   Returns ZUZU_OK, or ERR_BUFFULL when the ring is full. */
int txframe_init(txframe_t *f, uint16_t headroom);

/* Append "n" payload bytes after the current content; returns a pointer to the
   region to fill, or NULL on overflow. */
void *txframe_append(txframe_t *f, uint16_t n);

/* Prepend "n" header bytes in front of the current content; returns a pointer
   to the region to fill, or NULL when the reserved headroom is exhausted. */
void *txframe_prepend(txframe_t *f, uint16_t n);

/* Start and length of the bytes built so far. */
static inline uint8_t *txframe_data(txframe_t *f) { return f->slot->data + f->front; }
static inline uint16_t txframe_len(txframe_t *f)  { return (uint16_t)(f->end - f->front); }

#endif /* NETD_TXFRAME_H */
