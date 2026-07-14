#ifndef LMSG_H
#define LMSG_H

#include <stdint.h>
#include <mem.h>
#include "tcb.h"
#include <zuzu/types.h>

static inline void *lmsg_buf(void) {
    return zuzu_tcb()->lmsg_buf;
}

/* No truncation: oversized payloads are rejected (return 0), matching the
 * kernel's ERR_BADARG on lsend/lcall/lreply. Chunk or use shm instead. */
static inline int32_t lmsg_write(const void *src, uint32_t len) {
    if (len > LMSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(lmsg_buf(), src, len);
    return len;
}
/* The lmsg buffer is per-thread and VOLATILE. It is valid only
 * until this thread's next IPC call (including printf, which lsends to the
 * console server). After a recv/waitany that delivers an lmsg, copy the
 * payload out with lmsg_read() BEFORE doing anything else. */
static inline int32_t lmsg_read(void *dst, uint32_t len) {
    if (len > LMSG_BUF_SIZE)
        return ERR_OVERFLOW;
    memcpy(dst, lmsg_buf(), len);
    return len;
}

#endif // LMSG_H
