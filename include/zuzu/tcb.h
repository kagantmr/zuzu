#ifndef TCB_H
#define TCB_H

#include <stdint.h>
#include <stddef.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define TCB_HDR_SIZE    64
#define LMSG_BUF_SIZE   512
#define TCB_SLOT_SIZE   (TCB_HDR_SIZE + LMSG_BUF_SIZE)   /* 576 */
#define TCB_MAX_SLOTS   (PAGE_SIZE / TCB_SLOT_SIZE)      /* 7 */

typedef struct {
    void    *lmsg_buf;      /* → this slot's buf; kernel owns the location */
    uint32_t tid;
    uint32_t pid;
    uint8_t  _pad[TCB_HDR_SIZE - 12];
    uint8_t  buf[LMSG_BUF_SIZE];
} tdata_t;

_Static_assert(sizeof(tdata_t) == TCB_SLOT_SIZE, "tdata_t must fill its slot");
_Static_assert(TCB_SLOT_SIZE * TCB_MAX_SLOTS <= PAGE_SIZE, "slots overflow page");

static inline tdata_t *zuzu_tcb(void) {
    tdata_t *tcb;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tcb));
    return tcb;
}

#endif // TCB_H