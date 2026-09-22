#include "svc.h"
#include "kernel/sched/sched.h"
#include "kernel/task/task.h"

#define LOG_FMT(fmt) "(SvcQuit) " fmt
#include <zuzu/log.h>

void SvcQuit(CpuState *frame)
{
    int32_t exit_status = (int32_t)(*arch_reg(frame, 0));

    TaskTerminate(current_thread, exit_status);

    Schedule();
}
