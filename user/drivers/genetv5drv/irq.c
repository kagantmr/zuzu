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

uint32_t GenetIrqWait(void)
{
    EventWaitResult r = WaitOnEvent(g_genet.irq, 1000, 0, false);
    LOG_INFO(LOG_TAG, "dbg: wake st %d bits %x stat %08x mask %08x rxprod %u rxcons %u mibrx %u",
             r.status, r.bits, g_genet.intrl2_0->stat, g_genet.intrl2_0->mask_status,
             g_genet.rx_ring->prod_index & 0xffff, g_genet.rx_ring->cons_index & 0xffff,
             *(volatile uint32_t *)((uint8_t *)g_genet.umac + 0x428));
    if (r.status != ZUZU_OK)
        return 0;

    uint32_t stat = g_genet.intrl2_0->stat & ~g_genet.intrl2_0->mask_status;
    g_genet.intrl2_0->clear = stat;
    IrqRearm(g_genet.dev);
    return stat;
}
