#include "svc.h"
#include <arch/irq.h>
#include <arch/barrier.h>
#include "kernel/ipc/handle.h"

typedef struct irq_owner {
    ProcessObj *owner;
    bool pending;
    NtfnObj *bound_ntfn; // was Endpoint *bound_port
} IrqOwner;

static IrqOwner irq_owners[MAX_IRQS];

void SysIrqDone(CpuState *frame)
{
    /* dsb sy: the driver's MMIO writes that quiesced the device must complete
     * before we re-enable the line (see gic_end). */
    ArchDsbSy();

    Handle dev_handle = (Handle)(*arch_reg(frame, 0));

    if (dev_handle == 0) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    HandleEntry *entry = HandleTableGet(&current_thread->owner_process->handle_table, (uint32_t)dev_handle);
    if (!entry) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (entry->type != HANDLE_DEVICE) {
        arch_reg_set(frame, 0, ERR_BADTYPE);
        return;
    }
    if (!entry->dev) {
        arch_reg_set(frame, 0, ERR_BADHANDLE);
        return;
    }
    if (!(entry->dev->irq < MAX_IRQS) || arch_irq_is_reserved(entry->dev->irq)) {
        arch_reg_set(frame, 0, ERR_BADARG);
        return;
    }
    if (irq_owners[entry->dev->irq].owner == current_thread->owner_process) {
        arch_irq_enable_line(entry->dev->irq);
        (*arch_reg(frame, 0)) = 0;
        return;
    } else {
        arch_reg_set(frame, 0, ERR_NOPERM);
        return;
    }
}