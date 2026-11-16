#include "genet.h"
#include <util/log.h>

Err GenetIrqInit(void)
{
    g_genet.intrl2_0->mask_set = 0xffffffff;
    g_genet.intrl2_0->clear = 0xffffffff;

    g_genet.irq = CreateEvent();
    if (g_genet.irq < 0) {
        LOG_ERROR(LOG_TAG, "CreateEvent failed: %s", StrToError(g_genet.irq));
        return g_genet.irq;
    }
    Err rc = BindIrq(g_genet.irq, g_genet.dev, 0);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "BindIrq failed: %s", StrToError(rc));
        return rc;
    }
    return ZUZU_OK;
}

void GenetIrqEnable(uint32_t bits) { g_genet.intrl2_0->mask_clear = bits; }

uint32_t GenetIrqWait(Duration timeout)
{
    EventWaitResult r = WaitOnEvent(g_genet.irq, timeout, 0, false);
    if (r.status != ZUZU_OK)
        return 0;

    uint32_t stat = g_genet.intrl2_0->stat & ~g_genet.intrl2_0->mask_status;
    g_genet.intrl2_0->clear = stat;
    IrqRearm(g_genet.dev);
    return stat;
}
