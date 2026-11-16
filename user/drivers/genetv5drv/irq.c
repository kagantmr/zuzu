#include "genet.h"
#include <util/log.h>

Err GenetIrqInit(void)
{
    g_genet.intrl2_0->mask_set = 0xffffffff;
    g_genet.intrl2_0->clear = 0xffffffff;

    g_genet.event = CreateEvent();
    if (g_genet.event < 0) {
        LOG_ERROR(LOG_TAG, "CreateEvent failed: %s", StrToError(g_genet.event));
        return g_genet.event;
    }
    Err rc = BindIrq(g_genet.event, g_genet.dev, GENET_IRQ_BIT);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "BindIrq failed: %s", StrToError(rc));
        return rc;
    }
    return ZUZU_OK;
}

void GenetIrqEnable(uint32_t bits) { g_genet.intrl2_0->mask_clear = bits; }

void GenetIrqDisable(uint32_t bits) { g_genet.intrl2_0->mask_set = bits; }

uint32_t GenetIrqAck(void)
{
    uint32_t stat = g_genet.intrl2_0->stat & ~g_genet.intrl2_0->mask_status;
    g_genet.intrl2_0->clear = stat;
    g_genet.irqs++;
    IrqRearm(g_genet.dev);
    return stat;
}
