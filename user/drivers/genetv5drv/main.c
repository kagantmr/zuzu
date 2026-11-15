#include "genet.h"
#include <string.h>
#include <util/log.h>

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

    uint32_t umac_speed = 0, mbps = 0;
    rc = PhyNegotiate();
    if (rc == ZUZU_OK)
        rc = PhyGetSpeed(&umac_speed, &mbps);
    if (rc != ZUZU_OK)
        return rc;
    SetLink(umac_speed);

    LOG_INFO(LOG_TAG, "link %u Mb/s; cmd 0x%08x oob 0x%08x mac0 0x%08x mac1 0x%08x", mbps,
             g_genet.umac->cmd, g_genet.ext->rgmii_oob_ctrl, g_genet.umac->mac0,
             g_genet.umac->mac1);

    rc = GenetTxInit();
    if (rc != ZUZU_OK)
        return rc;

    static const char payload[] = "hello from zuzu";
    uint8_t frame[60] = {0};
    memset(frame, 0xff, 6);
    memcpy(frame + 6, g_genet.mac, 6);
    frame[12] = 0x88;
    frame[13] = 0xb5;
    memcpy(frame + 14, payload, sizeof(payload));
    GenetTx(frame, sizeof(frame));

    rc = GenetRxInit();
    if (rc != ZUZU_OK)
        return rc;

    GenetIrqEnable(RXDMA_MBDONE);

    for (;;) {
        uint32_t stat = GenetIrqWait();
        if (stat & RXDMA_MBDONE)
            GenetRxPoll();
    }
}
