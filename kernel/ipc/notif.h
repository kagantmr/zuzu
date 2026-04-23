#ifndef NOTIF_H
#define NOTIF_H


#include <list.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct notification {
    uint32_t word;              // 32-bit signal mask, atomic-ish (IRQs off)
    list_head_t wait_queue;     // processes blocked in ntfn_wait
    uint32_t owner_pid;
    uint32_t ref_count;
    bool alive;
} notification_t;



#endif // NOTIF_H