#ifndef NOTIF_H
#define NOTIF_H


#include <list.h>
#include <zuzu/types.h>
#include <stdbool.h>

typedef struct notification {
    NtfnBits word;              // 31-bit signal mask (bit 31 reserved), atomic-ish (IRQs off)
    ListHead wait_queue;     // processes blocked in ntfn_wait
    Pid owner_pid;
    size_t ref_count;
    bool alive;
} Ntfn;



#endif // NOTIF_H
