/**
 * @file svc_hold.c
 * @brief Implementation of the Hold() svc.
 */

#include "kernel/ipc/msg.h"
#include "svc.h"
#include <arch/regs.h>

void SvcHold(CpuState *frame)
{
    Ticket t = 0;
    Err rc = PortHoldCurrent(current_task, &t);
    ArchSetInFrame(frame, 0, rc);
    if (rc == ZUZU_OK)
        ArchSetInFrame(frame, 1, (Register)t);
}
