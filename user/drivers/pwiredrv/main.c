/**
 * @file main.c
 *
 * @brief pwiredrv: a pseudo wire between two netd instances.
 *
 * Usage: pwiredrv <service-a> <service-b>. The driver does the NIC handshake with both and copies
 * every frame one side transmits into the other side's rx ring.
 */

#include <net/packetring.h>
#include <net/protocols/netd.h>
#include <net/protocols/nic.h>
#include <stdio.h>
#include <string.h>
#include <util/log.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define LOG_TAG "pwiredrv"

/** @brief Size of the interface name netd sends back. */
#define PWIRE_NAME_MAX 8

/** @brief One end of the wire: the rings shared with one netd. */
typedef struct {
    const char *service;        /**< netd service this end attaches to. */
    MacAddr mac;                /**< MAC address this end reports. */
    Handle tx_mem;              /**< Memory of the ring netd transmits into. */
    Handle rx_mem;              /**< Memory of the ring netd receives from. */
    NicRing *tx_ring;           /**< Frames netd sends onto the wire. */
    NicRing *rx_ring;           /**< Frames the wire delivers to netd. */
    NicStatsBlock *stats;       /**< Counters and link state netd reads. */
    Handle netd_bell;           /**< netd's event, signalled when rx frames are queued. */
    char ifname[PWIRE_NAME_MAX]; /**< Name netd gave this end. */
} WireEnd;

static WireEnd g_end[2];
static Handle g_doorbell = -1;

static Err MapRing(Handle *mem, NicRing **ring)
{
    *mem = CreateMem(NIC_SHM_BYTES / 4096, 0);
    if (*mem < 0)
        return *mem;
    void *va = MemMap(*mem, 0, PROT_RW);
    if (PtrIsErr(va))
        return (Err)(VirtAddr)va;
    memset(va, 0, NIC_SHM_BYTES);
    *ring = (NicRing *)va;
    return ZUZU_OK;
}

static Err Grant(Handle port, const MsgWriter *w, Handle h, HandlePerms perms, SvcResult *out)
{
    if (w->ovf)
        return ERR_OVERFLOW;
    SvcResult dup = HandleDuplicate(h, perms | PERM_TXFR, MARKER_NONE);
    if (dup.r0 != ZUZU_OK)
        return (Err)dup.r0;
    SvcResult r = Call(port, w->off, (Handle)dup.r1);
    HandleClose((Handle)dup.r1);
    if (r.r0 != ZUZU_OK)
        return (Err)r.r0;
    Err status;
    if ((uint32_t)r.r1 < sizeof(status))
        return ERR_MALFORMED;
    memcpy(&status, GetMessageBox(), sizeof(status));
    if (status == ZUZU_OK && out)
        *out = r;
    return status;
}

static Err Handshake(WireEnd *e)
{
    Handle port = WaitForService(e->service);
    if (port < 0)
        return port;

    uint32_t mac_lo = 0, mac_hi = 0;
    memcpy(&mac_lo, &e->mac[0], 4);
    memcpy(&mac_hi, &e->mac[4], 2);

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutByte(&w, NETD_DRVHANDSHAKE_INIT);
    MsgPutU32(&w, mac_lo);
    MsgPutU32(&w, mac_hi);
    MsgPutU64(&w, 0);
    MsgPutByte(&w, NETD_IFTYPE_PWIRE);
    Err rc = Grant(port, &w, g_doorbell, PERM_WAIT | PERM_SEND, NULL);
    if (rc != ZUZU_OK)
        return rc;

    MsgWriterInit(&w);
    MsgPutByte(&w, NETD_DRVHANDSHAKE_STAGE2);
    rc = Grant(port, &w, e->tx_mem, PERM_MAP | PERM_WRITE, NULL);
    if (rc != ZUZU_OK)
        return rc;

    SvcResult reply;
    MsgWriterInit(&w);
    MsgPutByte(&w, NETD_DRVHANDSHAKE_STAGE3);
    rc = Grant(port, &w, e->rx_mem, PERM_MAP | PERM_WRITE, &reply);
    if (rc != ZUZU_OK)
        return rc;

    uint32_t len = (uint32_t)reply.r1 - sizeof(Err);
    if (len == 0 || len > sizeof(e->ifname))
        return ERR_MALFORMED;
    memcpy(e->ifname, (const char *)GetMessageBox() + sizeof(Err), len);
    if (e->ifname[len - 1] != '\0')
        return ERR_MALFORMED;
    e->netd_bell = (Handle)reply.r3;
    if (e->netd_bell < 0)
        return ERR_MALFORMED;
    HandleClose(port);
    return ZUZU_OK;
}

/** @brief Move every frame @p from transmitted into @p to's rx ring. */
static void Carry(WireEnd *from, WireEnd *to)
{
    bool delivered = false;
    NicFrame *f;
    while ((f = PacketRingPeek(from->tx_ring)) != NULL) {
        from->stats->stat[NIC_STAT_TX_PACKETS]++;
        if (PacketRingPush(to->rx_ring, f->data, (uint16_t)f->len) < 0) {
            to->stats->stat[NIC_STAT_RX_RING_FULL]++;
        } else {
            to->stats->stat[NIC_STAT_RX_PACKETS]++;
            delivered = true;
        }
        PacketRingConsume(from->tx_ring);
    }
    if (delivered)
        Signal(to->netd_bell, NIC_NETD_DOORBELL, false);
}

/** @brief FNV-1a over both service names, so each wire gets its own MAC pair. */
static uint32_t WireHash(const char *a, const char *b)
{
    uint32_t h = 2166136261u;
    for (const char *p = a; *p; p++)
        h = (h ^ (uint8_t)*p) * 16777619u;
    h = (h ^ '|') * 16777619u;
    for (const char *p = b; *p; p++)
        h = (h ^ (uint8_t)*p) * 16777619u;
    return h;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        printf("usage: pwiredrv <netd-service-a> <netd-service-b>\n");
        return ERR_BADARG;
    }

    g_doorbell = CreateEvent();
    if (g_doorbell < 0)
        return g_doorbell;

    for (int i = 0; i < 2; i++) {
        WireEnd *e = &g_end[i];
        e->service = argv[1 + i];
        static const MacAddr base = {0x02, 0x70, 0x77, 0x00, 0x00, 0x00};
        memcpy(e->mac, base, sizeof(e->mac));
        uint32_t h = WireHash(argv[1], argv[2]);
        e->mac[3] = (uint8_t)(h >> 8);
        e->mac[4] = (uint8_t)h;
        e->mac[5] = (uint8_t)(i + 1);
        Err rc = MapRing(&e->tx_mem, &e->tx_ring);
        if (rc == ZUZU_OK)
            rc = MapRing(&e->rx_mem, &e->rx_ring);
        if (rc != ZUZU_OK) {
            LOG_ERROR(LOG_TAG, "ring setup failed: %s", StrToError(rc));
            return rc;
        }
        e->stats = (NicStatsBlock *)((uint8_t *)e->rx_ring + NIC_STATS_OFFSET);
    }

    for (int i = 0; i < 2; i++) {
        Err rc = Handshake(&g_end[i]);
        if (rc != ZUZU_OK) {
            LOG_ERROR(LOG_TAG, "handshake with %s failed: %s", g_end[i].service, StrToError(rc));
            return rc;
        }
        LOG_INFO(LOG_TAG, "%s is %s on %s", g_end[i].service, g_end[i].ifname,
                 i == 0 ? "end a" : "end b");
    }

    for (;;) {
        EventWaitResult res = WaitOnEvent(g_doorbell, TIMEOUT_INFINITE, NIC_DOORBELL_TX, false);
        if (res.status != ZUZU_OK)
            continue;
        Carry(&g_end[0], &g_end[1]);
        Carry(&g_end[1], &g_end[0]);
    }
}
