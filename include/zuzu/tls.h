#ifndef TCB_H
#define TCB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <arch/tls.h>
#include <zuzu/types.h>
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
    void    *lmsg_buf;      /* this slot's buf; kernel owns the location */
    Tid tid;
    Pid pid;
    uint8_t  _pad[TCB_HDR_SIZE - 12];
    uint8_t  buf[LMSG_BUF_SIZE];
} ThreadData;

_Static_assert(sizeof(ThreadData) == TCB_SLOT_SIZE, "ThreadData must fill its slot");
_Static_assert(TCB_SLOT_SIZE * TCB_MAX_SLOTS <= PAGE_SIZE, "slots overflow page");

/**
 * TLS accessor for the current thread's TCB.
 */
static inline ThreadData *ZuzuTLS(void) {
    return (ThreadData *)arch_get_thread_ptr();
}

#ifdef __cplusplus
}
#endif

#endif // TCB_H