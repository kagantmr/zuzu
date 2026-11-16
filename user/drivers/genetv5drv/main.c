#include "genet.h"
#include <util/log.h>
#include <zuzu/uptime.h>

static void RxFrame(const uint8_t *f, uint32_t len)
{
    LOG_DEBUG(LOG_TAG, "rx %u bytes from %02x:%02x:%02x:%02x:%02x:%02x type %02x%02x", len, f[6],
              f[7], f[8], f[9], f[10], f[11], f[12], f[13]);
}

int main()
{
    Err rc = MapRegs();
    if (rc == ZUZU_OK)
        rc = CheckRev();
    if (rc == ZUZU_OK)
        rc = GetMac();
    if (rc == ZUZU_OK)
        rc = DmaStop();
    if (rc == ZUZU_OK)
        rc = GenetIrqInit();
    if (rc != ZUZU_OK)
        return rc;

    ExtPowerUp();
    UmacReset();
    WriteMac();

    uint32_t umac_speed = 0;
    rc = PhyNegotiate();
    if (rc == ZUZU_OK)
        rc = PhyGetSpeed(&umac_speed, &g_genet.mbps);
    if (rc != ZUZU_OK)
        return rc;
    SetLink(umac_speed);
    g_genet.link_up = true;

    rc = GenetTxInit();
    if (rc == ZUZU_OK)
        rc = GenetRxInit();
    if (rc != ZUZU_OK)
        return rc;
    GenetIrqEnable(RXDMA_MBDONE);
    LOG_INFO(LOG_TAG, "up: %u Mb/s, MAC %02x:%02x:%02x:%02x:%02x:%02x", g_genet.mbps,
             g_genet.mac[0], g_genet.mac[1], g_genet.mac[2], g_genet.mac[3], g_genet.mac[4],
             g_genet.mac[5]);

    uint64_t last_poll = UptimeMs();
    for (;;) {
        uint32_t stat = GenetIrqWait(GENET_LINK_POLL_MS);
        if (stat & RXDMA_MBDONE)
            GenetRxPoll(RxFrame);

        uint64_t now = UptimeMs();
        if (now - last_poll >= GENET_LINK_POLL_MS) {
            PhyPollLink();
            last_poll = now;
        }
    }
}
