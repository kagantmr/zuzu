#ifndef EVENT_H
#define EVENT_H

#include <zuzu/types.h>
#include "syscall_nums.h"
#include <arch/syscall.h>

#define KEVENT_MEMMGMT_BIT (1u << 0)

static inline void *ZuzuKEventBind(KEventType eventtype, Handle ntfn, void *args) {
    return (void *)(VirtAddr)Syscall(SYS_KEVENT_BIND, eventtype, (uint32_t)ntfn, (VirtAddr)args, 0);
}

#endif /* EVENT_H */