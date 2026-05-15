#ifndef TCB_H
#define TCB_H

#include <stdint.h>

#define TCB_SLOT_SIZE   64   // pad for future fields
#define TCB_MAX_SLOTS   (4096 / TCB_SLOT_SIZE)  // 64 threads per process


typedef struct {
    void    *ipc_buf;
    uint32_t tid;
    uint32_t pid;
} tdata_t;

static inline tdata_t *__zuzu_tcb(void) {
    tdata_t *tcb;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tcb));
    return tcb;
}

#endif // TCB_H