#include "kernel/svc/svc.h"
#include <arch/regs.h>
#include <compiler.h>

void __hot SvcTrap(CpuState *frame);

void __hot SvcTrap(CpuState *frame)
{
    if (unlikely((frame->return_cpsr & 0x1F) != 0x10))
        return;

    uint8_t svc_num;
    if (unlikely(frame->return_cpsr & (1 << 5))) {
        uint16_t *thumb_instr = (uint16_t *)(frame->return_pc - 2);
        svc_num = (uint8_t)(*thumb_instr & 0xFF);
    } else {
        uint32_t *arm_instr = (uint32_t *)(frame->return_pc - 4);
        svc_num = (uint8_t)(*arm_instr & 0xFF);
    }

    SvcDispatch(svc_num, frame);
}