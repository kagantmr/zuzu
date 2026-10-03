#include "svc.h"
#include "kernel/space/space.h"
#include "core/log.h"
#include <arch/regs.h>

#ifdef DEBUG
#define SYSLOG_MAX 240u
void SvcDebugLog(CpuState *frame)
{
    VirtAddr uptr = (VirtAddr)(*ArchGetFromFrame(frame, 0));
    size_t len = (size_t)(*ArchGetFromFrame(frame, 1));
    char buf[SYSLOG_MAX + 1];

    if (len > SYSLOG_MAX)
        len = SYSLOG_MAX;
    if (len == 0 || !CopyFromUser(buf, (const void *)uptr, len)) {
        ArchSetInFrame(frame, 0, ERR_BADPTR);
        return;
    }
    buf[len] = '\0';
    kprintf("[udbg spid=%u] %s\n",
            (unsigned)(CURRENT_SPACE ? CURRENT_SPACE->spid : 0), buf);
    ArchSetInFrame(frame, 0, 0);
}
#endif /* DEBUG */