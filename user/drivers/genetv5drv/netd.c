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
static Handle netd_bell = -1;
static NicRing *nic_rx, *nic_tx;
static NicStatsBlock *nic_block;
static volatile uint32_t *nic_stats;
static char ifname[16];
static bool rx_pushed;

/** @brief Unmap and close whatever part of the packet rings exists, and reset the globals. */
static void FreePacketRings(void)
{
    if (nic_tx)
        MemUnmap(nic_tx);
    if (nic_rx)
        MemUnmap(nic_rx);
    if (shm_tx_handle >= 0)
        HandleClose(shm_tx_handle);
    if (shm_rx_handle >= 0)
        HandleClose(shm_rx_handle);
    nic_tx = NULL;
    nic_rx = NULL;
    nic_block = NULL;
    nic_stats = NULL;
    shm_tx_handle = -1;
    shm_rx_handle = -1;
}

/** @brief Create and map one shared ring; on error *h and *ring are left unset. */
static Err CreateRing(Handle *h, NicRing **ring)
{
    Handle mem = CreateMem(NIC_SHM_BYTES / 4096, 0);
    if (mem < 0) {
        LOG_ERROR(LOG_TAG, "CreateMem failed: %s", StrToError(mem));
        return mem;
    }
    *h = mem;
    void *va = MemMap(mem, 0, PROT_RW);
    if (PtrIsErr(va)) {
        LOG_ERROR(LOG_TAG, "MemMap on ring failed: %s", StrToError((Err)(VirtAddr)va));
        return (Err)(VirtAddr)va;
    }
    *ring = (NicRing *)va;
    return ZUZU_OK;
}

static Err InitPacketRings(void)
{
    Err rc = CreateRing(&shm_tx_handle, &nic_tx);
    if (rc == ZUZU_OK)
        rc = CreateRing(&shm_rx_handle, &nic_rx);
    if (rc != ZUZU_OK) {
        FreePacketRings();
        return rc;
    }

    nic_block = (NicStatsBlock *)((uint8_t *)nic_rx + NIC_STATS_OFFSET);
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

/** @brief Run the three handshake stages on @p netd_port and store the name and doorbell. */
static Err NetdHandshakeOn(Handle netd_port)
{
    uint32_t mac_lo = 0, mac_hi = 0;
    memcpy(&mac_lo, &g_genet.mac[0], 4);
    memcpy(&mac_hi, &g_genet.mac[4], 2);
    uint64_t driver_key = 0;
    for (size_t i = 0; i < sizeof(g_genet.mac); i++)
        driver_key = (driver_key << 8) | g_genet.mac[i];

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutByte(&w, NETD_DRVHANDSHAKE_INIT);
    MsgPutU32(&w, mac_lo);
    MsgPutU32(&w, mac_hi);
    MsgPutU64(&w, driver_key);
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

    Handle bell = (Handle)reply.r3;
    uint32_t len = (uint32_t)reply.r1 - sizeof(Err);
    if (bell < 0 || len == 0 || len > sizeof(ifname)) {
        if (bell >= 0)
            HandleClose(bell);
        return ERR_MALFORMED;
    }
    memcpy(ifname, (const char *)GetMessageBox() + sizeof(Err), len);
    if (ifname[len - 1] != '\0') {
        HandleClose(bell);
        return ERR_MALFORMED;
    }
    netd_bell = bell;

    LOG_INFO(LOG_TAG, "registered with netd as %s", ifname);
    return ZUZU_OK;
}

/** @brief Find netd, run the handshake, and close the service port afterwards. */
static Err NetdHandshake(void)
{
    Handle netd_port = WaitForService("/svc/netd");
    if (netd_port < 0) {
        LOG_ERROR(LOG_TAG, "Couldn't find netd");
        return ERR_SYSDOWN;
    }
    Err rc = NetdHandshakeOn(netd_port);
    HandleClose(netd_port);
    return rc;
}

Err ConnectToNetd(void)
{
    Err rc = InitPacketRings();
    if (rc == ZUZU_OK)
        rc = NetdHandshake();
    if (rc != ZUZU_OK)
        FreePacketRings();
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
        Signal(netd_bell, NIC_NETD_DOORBELL, false);
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
    Signal(netd_bell, NIC_NETD_DOORBELL, false);
}
