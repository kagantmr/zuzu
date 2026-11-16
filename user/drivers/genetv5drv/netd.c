#include "netd.h"
#include "genet.h"
#include <net/packetring.h>
#include <net/protocols/netd.h>
#include <net/protocols/nic.h>
#include <string.h>
#include <util/log.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

static Handle shm_tx_handle = -1, shm_rx_handle = -1;
static NicRing *nic_rx, *nic_tx;
static NicStatsBlock *nic_block;
static volatile uint32_t *nic_stats;
static char ifname[16];
static bool rx_pushed;

static Err InitPacketRings(void)
{
    shm_tx_handle = CreateMem(NIC_SHM_BYTES / 4096, 0);
    if (shm_tx_handle < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(shm_tx_handle));
        return shm_tx_handle;
    }
    void *shm_tx = MemMap(shm_tx_handle, 0, PROT_RW);
    if (PtrIsErr(shm_tx)) {
        LOG_ERROR(LOG_TAG, "MemMap on shm_tx failed: %s", StrToError((Err)(VirtAddr)shm_tx));
        return (Err)(VirtAddr)shm_tx;
    }

    shm_rx_handle = CreateMem(NIC_SHM_BYTES / 4096, 0);
    if (shm_rx_handle < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(shm_rx_handle));
        return shm_rx_handle;
    }
    void *shm_rx = MemMap(shm_rx_handle, 0, PROT_RW);
    if (PtrIsErr(shm_rx)) {
        LOG_ERROR(LOG_TAG, "MemMap on shm_rx failed: %s", StrToError((Err)(VirtAddr)shm_rx));
        return (Err)(VirtAddr)shm_rx;
    }

    nic_rx = (NicRing *)shm_rx;
    nic_tx = (NicRing *)shm_tx;
    nic_block = (NicStatsBlock *)((uint8_t *)shm_rx + NIC_STATS_OFFSET);
    nic_stats = nic_block->stat;
    memset(nic_block, 0, sizeof(NicStatsBlock));
    nic_block->link_down = !g_genet.link_up;

    nic_tx->head = 0;
    nic_tx->tail = 0;
    nic_rx->head = 0;
    nic_rx->tail = 0;
    return ZUZU_OK;
}

static Err NetdGrant(Handle netd_port, const MsgWriter *w, Handle h, HandlePerms perms,
                     SvcResult *out)
{
    if (w->ovf)
        return ERR_OVERFLOW;

    SvcResult dup = HandleDuplicate(h, perms | PERM_TXFR, MARKER_NONE);
    if (dup.r0 != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "duplicate failed (handle %d): %s", h, StrToError((Err)dup.r0));
        return (Err)dup.r0;
    }

    SvcResult r = Call(netd_port, w->off, (Handle)dup.r1);
    HandleClose((Handle)dup.r1);
    if (r.r0 != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "handshake call failed: %s", StrToError((Err)r.r0));
        return (Err)r.r0;
    }

    Err status;
    if ((uint32_t)r.r1 < sizeof(status))
        return ERR_MALFORMED;
    memcpy(&status, GetMessageBox(), sizeof(status));
    if (status != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "netd rejected handshake: %s", StrToError(status));
        return status;
    }

    if (out)
        *out = r;
    return ZUZU_OK;
}

static Err NetdHandshake(void)
{
    Handle netd_port = WaitForService("/svc/netd");
    if (netd_port < 0) {
        LOG_ERROR(LOG_TAG, "Couldn't find netd");
        return ERR_SYSDOWN;
    }

    uint32_t mac_lo = 0, mac_hi = 0;
    memcpy(&mac_lo, &g_genet.mac[0], 4);
    memcpy(&mac_hi, &g_genet.mac[4], 2);

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutByte(&w, NETD_DRVHANDSHAKE_INIT);
    MsgPutU32(&w, mac_lo);
    MsgPutU32(&w, mac_hi);
    Err rc = NetdGrant(netd_port, &w, g_genet.event, PERM_WAIT | PERM_SEND, NULL);
    if (rc != ZUZU_OK)
        return rc;

    MsgWriterInit(&w);
    MsgPutByte(&w, NETD_DRVHANDSHAKE_STAGE2);
    rc = NetdGrant(netd_port, &w, shm_tx_handle, PERM_MAP | PERM_WRITE, NULL);
    if (rc != ZUZU_OK)
        return rc;

    SvcResult reply;
    MsgWriterInit(&w);
    MsgPutByte(&w, NETD_DRVHANDSHAKE_STAGE3);
    rc = NetdGrant(netd_port, &w, shm_rx_handle, PERM_MAP | PERM_WRITE, &reply);
    if (rc != ZUZU_OK)
        return rc;

    uint32_t len = (uint32_t)reply.r1 - sizeof(Err);
    if (len == 0 || len > sizeof(ifname))
        return ERR_MALFORMED;
    memcpy(ifname, (const char *)GetMessageBox() + sizeof(Err), len);
    if (ifname[len - 1] != '\0')
        return ERR_MALFORMED;

    LOG_INFO(LOG_TAG, "registered with netd as %s", ifname);
    return ZUZU_OK;
}

Err ConnectToNetd(void)
{
    Err rc = InitPacketRings();
    if (rc == ZUZU_OK)
        rc = NetdHandshake();
    return rc;
}

static void PushFrame(const uint8_t *frame, uint32_t len)
{
    if (len > NIC_FRAME_SIZE) {
        nic_stats[NIC_STAT_RX_OVERSIZE]++;
        return;
    }
    if (PacketRingPush(nic_rx, (void *)frame, (uint16_t)len) != 0) {
        nic_stats[NIC_STAT_RX_RING_FULL]++;
        return;
    }
    nic_stats[NIC_STAT_RX_PACKETS]++;
    rx_pushed = true;
}

void NetdRx(void)
{
    rx_pushed = false;
    GenetRxPoll(PushFrame);
    if (rx_pushed)
        Signal(g_genet.event, NIC_DOORBELL_RX, false);
}

Err NetdTx(void)
{
    NicFrame *f;
    while ((f = PacketRingPeek(nic_tx)) != NULL) {
        Err rc = GenetTx(f->data, f->len);
        if (rc == ERR_BUSY)
            return ERR_BUSY;
        if (rc != ZUZU_OK)
            nic_stats[NIC_STAT_TX_DROPS]++;
        PacketRingConsume(nic_tx);
    }
    return ZUZU_OK;
}

void NetdSyncStats(void)
{
    nic_stats[NIC_STAT_IRQ] = g_genet.irqs;
    nic_stats[NIC_STAT_TX_PACKETS] = g_genet.tx_packets;
    nic_stats[NIC_STAT_RX_ERRORS] = g_genet.rx_errors;
}

void NetdReportLink(void)
{
    nic_block->link_down = !g_genet.link_up;
    Signal(g_genet.event, NIC_DOORBELL_LINK, false);
}
