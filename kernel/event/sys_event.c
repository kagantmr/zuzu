#include "sys_event.h"
#include <zuzu/types.h>

typedef enum {
    KEVENT_MEMMGMT = 0
} KEventType;

void SysKEventBind(CpuState *frame) {
    if (!frame) return;

    KEventType event_type = *(arch_reg(frame, 0));
    Handle ntfn = *(arch_reg(frame, 1));
    void *data = *(arch_reg(frame, 2));

    /**
     * The only KEvent we support as of now is KEVENT_MEMMGMT which is a memory pressure
     * notification. For future event classes, an event-specific struct will be copied
     * to kernel, 
     */
    (void)data;

}