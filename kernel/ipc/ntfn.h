#ifndef NOTIF_H
#define NOTIF_H


#include <list.h>
#include <stdint.h>
#include <zuzu/types.h>
#include <stdbool.h>

typedef struct notification {
    uint32_t word;              // 31-bit signal mask (bit 31 reserved), atomic-ish (IRQs off)
    list_head_t wait_queue;     // processes blocked in ntfn_wait
    zpid_t owner_pid;
    size_t ref_count;
    bool alive;
} notification_t;



#endif // NOTIF_H