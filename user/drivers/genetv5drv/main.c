#include "genet.h"
#include "netd.h"
#include <net/protocols/nic.h>
#include <util/log.h>
#include <zuzu/uptime.h>

/** @brief Stop DMA and restart both rings, dropping frames still queued for transmit. */
static void RestartRings(void)
{
    if (DmaStop() == ZUZU_OK) {
        GenetRxRingStart();
        GenetTxRingStart();
    }
}

/** @brief Bring up the hardware and register with netd; the caller cleans up on failure. */
static Err Start(void)
{
    Err rc = MapRegs();
    if (rc == ZUZU_OK)
        rc = CheckRev();
    if (rc == ZUZU_OK)
        rc = GetMac();
    if (rc == ZUZU_OK)
        rc = DmaStop();
    if (rc == ZUZU_OK)
        rc = DmaReclaim();
    if (rc == ZUZU_OK)
        rc = GenetIrqInit();
    if (rc != ZUZU_OK)
        return rc;

    ExtPowerUp();
    UmacReset();
    WriteMac();

    rc = PhyStartAutoNeg();
    if (rc == ZUZU_OK)
        rc = GenetTxInit();
    if (rc == ZUZU_OK)
        rc = GenetRxInit();
    if (rc == ZUZU_OK)
        rc = ConnectToNetd();
    return rc;
}

int main()
{
    Err rc = Start();
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "start-up failed: %s", StrToError(rc));
        GenetShutdown();
        return rc;
    }
    GenetIrqEnable(RXDMA_MBDONE | TBUF_UNDERRUN | RBUF_OVERFLOW);
    LOG_INFO(LOG_TAG, "Ready");

    uint64_t last_poll = UptimeMs();
    bool rx_more = false;
    for (;;) {
        EventWaitResult r =
            WaitOnEvent(g_genet.event, (rx_more ? TIMEOUT_POLL : GENET_LINK_POLL_MS),
                        GENET_IRQ_MASK | NIC_DOORBELL_TX, false);
        GenetTxReclaim();
        bool kick_tx = r.status == ZUZU_OK && (r.bits & NIC_DOORBELL_TX);
        bool do_rx = rx_more;
        if (r.status == ZUZU_OK && (r.bits & GENET_IRQ_MASK)) {
            uint32_t stat = GenetIrqAck();
            if (stat & TBUF_UNDERRUN)
                g_genet.tx_underruns++;
            if (stat & RBUF_OVERFLOW)
                g_genet.rx_overflows++;
            if (stat & ~(RXDMA_MBDONE | TXDMA_MBDONE | TBUF_UNDERRUN | RBUF_OVERFLOW))
                g_genet.irq_other++;
            if (stat & RXDMA_MBDONE)
                do_rx = true;
            if (stat & TXDMA_MBDONE) {
                GenetIrqDisable(TXDMA_MBDONE);
                kick_tx = true;
            }
        }

        if (do_rx)
            rx_more = NetdRx();

        if (kick_tx && NetdTx() == ERR_BUSY)
            GenetIrqEnable(TXDMA_MBDONE);

        uint64_t now = UptimeMs();
        if (now - last_poll >= GENET_LINK_POLL_MS) {
            bool was_up = g_genet.link_up;
            PhyPollLink();
            if (g_genet.link_up != was_up) {
                if (!g_genet.link_up)
                    RestartRings();
                NetdReportLink();
            }
            if (GenetTxStuck(now))
                RestartRings();
            NetdSyncStats();
            last_poll = now;
        }
    }
}
