#include <dev/devm.h>
#include <net/protocols/netd.h>
#include <net/protocols/nic.h>
#include <types.h>
#include <util/log.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#include "common/globals.h"
#include "common/netrand.h"
#include "common/timer.h"
#include "link/arp.h"
#include "link/eth.h"
#include "transport/port.h"
#include "transport/session.h"
#include "transport/socket.h"
#include "transport/tcp.h"
#include "transport/udp.h"
#include "app/dns.h"
#include "app/dhcp.h"
#include "ctl.h"

Handle g_svc_port = -1;
Handle g_event = -1;

#define LEGACY_POLL_CAP 50u
#define NETD_DEFAULT_SERVICE "/svc/netd"

/** @brief An address given on the command line as ifname=a.b.c.d/n. */
typedef struct {
    char name[NETIF_NAME_MAX]; /**< Interface it applies to. */
    Ipv4Addr ip;               /**< Address. */
    Ipv4Addr netmask;          /**< Mask built from the prefix length. */
} StaticAddr;

static const char *g_service = NETD_DEFAULT_SERVICE;
static StaticAddr g_static[NETIF_MAX];
static int g_static_count;

/** @brief Parse "ifname=a.b.c.d/n" into the next g_static entry; false if malformed. */
static bool ParseStaticAddr(const char *arg)
{
    const char *eq = strchr(arg, '=');
    const char *slash = eq ? strchr(eq, '/') : NULL;
    if (!eq || !slash || eq == arg || (size_t)(eq - arg) >= NETIF_NAME_MAX ||
        g_static_count >= NETIF_MAX)
        return false;

    char ipbuf[16];
    size_t iplen = (size_t)(slash - eq - 1);
    if (iplen == 0 || iplen >= sizeof(ipbuf))
        return false;
    memcpy(ipbuf, eq + 1, iplen);
    ipbuf[iplen] = '\0';

    unsigned prefix = 0;
    const char *p = slash + 1;
    if (!*p)
        return false;
    for (; *p; p++) {
        if (*p < '0' || *p > '9')
            return false;
        prefix = prefix * 10 + (unsigned)(*p - '0');
    }
    if (prefix > 32)
        return false;

    StaticAddr *s = &g_static[g_static_count];
    memset(s, 0, sizeof(*s));
    memcpy(s->name, arg, (size_t)(eq - arg));
    if (!Ipv4ToBytes(ipbuf, &s->ip))
        return false;
    uint32_t mask = prefix ? ~0u << (32 - prefix) : 0;
    s->netmask = htonl(mask);
    g_static_count++;
    return true;
}

static const StaticAddr *StaticAddrFor(const char *name)
{
    for (int i = 0; i < g_static_count; i++)
        if (!strncmp(g_static[i].name, name, NETIF_NAME_MAX))
            return &g_static[i];
    return NULL;
}

/**
 * UDP echo handler
 */
static void UdpEchoHandler(Ipv4Addr src_ip, NetPort src_port, NetPort dst_port, const uint8_t *data,
                           uint16_t len)
{
    LOG_DEBUG(LOG_TAG, "UDP echo from %u.%u.%u.%u:%d to port %d", IP4(src_ip), src_port, dst_port);
    UdpSend(src_ip, dst_port, src_port, data, len);
}

static void CheckLink(void)
{
    for (int i = 0; i < NETIF_MAX; i++) {
        NetInterface *nif = &netifs[i];
        if (!NetifAttached(nif))
            continue;
        bool up = !nif->drv.stats->link_down;
        if (up == nif->link_up)
            continue;
        nif->link_up = up;
        if (!up) {
            LOG_INFO(LOG_TAG, "%s: link down", nif->name);
            ArpFlush(nif);
            DhcpLinkDown(nif);
        } else {
            LOG_INFO(LOG_TAG, "%s: link up", nif->name);
            DhcpLinkUp(nif);
        }
    }
}

/* Fires once the lease is first acquired: the network is now usable. */
static __attribute__((cold)) void OnDhcpBound(NetInterface *nif)
{
    LOG_INFO(LOG_TAG, "network up: %s ip %u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u",
             nif->name, IP4(nif->ip), IP4(nif->gateway), IP4(nif->dns));
}

static Err InitNetdServices(void)
{

    Err retval = ZUZU_OK;

    g_svc_port = CreatePort();
    if (g_svc_port < 0) {
        return g_svc_port;
    }

    g_event = CreateEvent();
    if (g_event < 0)
        return g_event;

    retval = RegisterService(g_service, g_svc_port);
    if (retval < 0) {
        LOG_ERROR(LOG_TAG, "registration failed");
        return retval;
    }

    return retval;
}

/** @brief Publish @p req in @p nif's control block and tell its driver to apply it. */
static Err SetRxFilter(NetInterface *nif, const NetdRxFilter *req)
{
    NicCntlBlock *cntl = nif->drv.cntl;
    for (uint32_t i = 0; i < req->mcast_count; i++)
        for (uint32_t b = 0; b < MAC_ADDR_LEN; b++)
            cntl->mcast[i][b] = req->mcast[i][b];
    cntl->mcast_count = req->mcast_count;
    cntl->flags = req->flags & (NIC_CNTL_PROMISC | NIC_CNTL_ALLMULTI);
    return Signal(nif->drv.doorbell, NIC_DOORBELL_CNTL, false);
}

/** @brief First attached interface at slot @p start or later, then lo, or NULL. */
static NetInterface *NetifFrom(unsigned start)
{
    for (unsigned i = start; i < NETIF_MAX; i++)
        if (NetifAttached(&netifs[i]))
            return &netifs[i];
    return start <= NETIF_MAX ? &netif_lo : NULL;
}

static void CtlReject(Err err, Handle granted)
{
    if (granted >= 0)
        HandleClose(granted);
    MsgWrite(&err, sizeof(err));
    CtlReply(sizeof(err), -1);
}

static void HandleNetdCall(const PortWaitResult *call)
{
    uint8_t op = call->xlen >= 1 ? *(const uint8_t *)GetMessageBox() : 0;
    switch (op) {
    case NETD_GET_NETIF: {
        if (call->granted >= 0)
            HandleClose(call->granted);

        unsigned start = call->xlen >= 2 ? ((const uint8_t *)GetMessageBox())[1] : 0;
        NetInterface *nif = NetifFrom(start);
        if (!nif) {
            CtlReject(ERR_NOENT, -1);
            return;
        }

        const NicStatsBlock *stats = nif->drv.stats;
        const NicCntlBlock *cntl = nif->drv.cntl;
        NetdNetif out = {.index = nif->index,
                         .ip = nif->ip,
                         .netmask = nif->netmask,
                         .gateway = nif->gateway,
                         .dns = nif->dns,
                         .link_up = !stats->link_down,
                         .rx_flags = cntl->flags};
        memcpy(out.mac, nif->mac, sizeof(out.mac));
        strncpy(out.name, nif->name, sizeof(out.name) - 1);
        for (int i = 0; i < NIC_STAT_COUNT; i++)
            out.stat[i] = stats->stat[i];
        out.mcast_count = cntl->mcast_count > NIC_MCAST_MAX ? NIC_MCAST_MAX : cntl->mcast_count;
        for (uint32_t i = 0; i < out.mcast_count; i++)
            for (uint32_t b = 0; b < MAC_ADDR_LEN; b++)
                out.mcast[i][b] = cntl->mcast[i][b];
        MsgWrite(&out, sizeof(out));
        CtlReply(sizeof(out), -1);
    } break;
    case NETD_SET_RXFILTER: {
        if (call->granted >= 0)
            HandleClose(call->granted);

        Err rc = ZUZU_OK;
        NetdRxFilter req;
        if (call->xlen < sizeof(req)) {
            rc = ERR_BADARG;
        } else {
            memcpy(&req, GetMessageBox(), sizeof(req));
            if (req.index >= NETIF_MAX || !NetifAttached(&netifs[req.index]))
                rc = ERR_NOENT;
            else if (req.mcast_count > NIC_MCAST_MAX)
                rc = ERR_BADARG;
            else
                rc = SetRxFilter(&netifs[req.index], &req);
        }
        MsgWrite(&rc, sizeof(rc));
        CtlReply(sizeof(rc), -1);
    } break;
    default: {
        CtlReject(ERR_NOSYS, call->granted);
        return;
    }
    }
}

/** @brief tcpip side of a driver handshake: installs the rings the ctl thread mapped. */
static __attribute__((cold)) void ApplyAttach(const CtlAttach *a)
{
    NetInterface *nif = &netifs[a->slot];
    NetifDriver old = nif->drv;

    memcpy(nif->mac, a->mac, sizeof(nif->mac));
    memcpy(nif->name, a->name, sizeof(nif->name));
    nif->drv.tx_ring = a->tx_ring;
    nif->drv.rx_ring = a->rx_ring;
    nif->drv.stats = (NicStatsBlock *)((uint8_t *)a->rx_ring + NIC_STATS_OFFSET);
    nif->drv.cntl = (NicCntlBlock *)((uint8_t *)a->rx_ring + NIC_CNTL_OFFSET);
    nif->drv.doorbell = a->doorbell;
    nif->link_up = !nif->drv.stats->link_down;

    if (!a->reattach) {
        const StaticAddr *s = StaticAddrFor(nif->name);
        if (!s) {
            DhcpStart(nif);
            return;
        }
        nif->ip = s->ip;
        nif->netmask = s->netmask;
        LOG_INFO(LOG_TAG, "%s: static %u.%u.%u.%u mask %u.%u.%u.%u", nif->name, IP4(nif->ip),
                 IP4(nif->netmask));
        return;
    }

    if (old.cntl) {
        for (uint32_t i = 0; i < NIC_MCAST_MAX; i++)
            for (uint32_t b = 0; b < MAC_ADDR_LEN; b++)
                nif->drv.cntl->mcast[i][b] = old.cntl->mcast[i][b];
        nif->drv.cntl->mcast_count = old.cntl->mcast_count;
        nif->drv.cntl->flags = old.cntl->flags;
        Signal(nif->drv.doorbell, NIC_DOORBELL_CNTL, false);
    }
    if (old.tx_ring)
        MemUnmap(old.tx_ring);
    if (old.rx_ring)
        MemUnmap(old.rx_ring);
    if (old.doorbell >= 0)
        HandleClose(old.doorbell);

    ArpFlush(nif);
    DhcpLinkUp(nif);
}

int main(int argc, char **argv)
{
    Err retval = ZUZU_OK;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '/') {
            g_service = argv[i];
        } else if (!ParseStaticAddr(argv[i])) {
            LOG_ERROR(LOG_TAG, "bad argument %s (want /svc/name or ifname=a.b.c.d/n)", argv[i]);
            return ERR_BADARG;
        }
    }

    NetifInit();

    retval = InitNetdServices();
    if (retval < 0)
        return retval;

    TimerInit();
    NetRandInit();

    retval = SessionInit(g_event);
    if (retval < 0)
        return retval;

    ArpInit();
    UdpInit();
    PortInit();
    DnsInit();
    DhcpInit(OnDhcpBound); /* each attached netif starts its client; OnDhcpBound fires when bound */
    
    UdpBind(7, UdpEchoHandler);

    retval = CtlStart();
    if (retval < 0)
        return retval;

    LOG_INFO(LOG_TAG, "online");

    while (1) {
        /* 1. size the sleep: until the next timer, but never longer than the
            cap, so the legacy pollers still get serviced. */
        uint32_t now = NetNowMs();
        uint32_t next = TimerNextDeadline();
        uint32_t sleep_ms;
        if (next == TIMER_NO_DEADLINE)
            sleep_ms = LEGACY_POLL_CAP;
        else if ((int32_t)(next - now) <= 0)
            sleep_ms = TIMEOUT_POLL; /* already overdue: don't block */
        else
            sleep_ms = next - now > LEGACY_POLL_CAP ? LEGACY_POLL_CAP : next - now;
        if (PacketRingPeek(netif_lo.drv.rx_ring))
            sleep_ms = TIMEOUT_POLL;

        /* 2. one wait for the driver, the ctl mailbox and every session */
        EventWaitResult ev = WaitOnEvent(g_event, sleep_ms, 0, false);
        CheckLink();

        /* 3. DRAIN RX FIRST: process inbound before any timer fires */
        NicFrame *frame;
        for (int i = 0; i < NETIF_MAX; i++) {
            while ((frame = PacketRingPeek(netifs[i].drv.rx_ring)) != NULL) {
                EthRx(&netifs[i], frame->data, (uint16_t)frame->len);
                PacketRingConsume(netifs[i].drv.rx_ring);
            }
        }
        for (int budget = NIC_RING_DEPTH;
             budget > 0 && (frame = PacketRingPeek(netif_lo.drv.rx_ring)) != NULL; budget--) {
            netif_lo.drv.stats->stat[NIC_STAT_TX_PACKETS]++;
            netif_lo.drv.stats->stat[NIC_STAT_RX_PACKETS]++;
            EthRx(&netif_lo, frame->data, (uint16_t)frame->len);
            PacketRingConsume(netif_lo.drv.rx_ring);
        }

        PortWaitResult call;
        while (CtlNextCall(&call)) {
            if (call.xlen == sizeof(CtlAttach) &&
                *(const uint8_t *)GetMessageBox() == CTL_OP_ATTACH) {
                CtlAttach a;
                memcpy(&a, GetMessageBox(), sizeof(a));
                ApplyAttach(&a);
                continue;
            }
            if (!SessionHandle(&call))
                HandleNetdCall(&call);
        }
        if (ev.status == ZUZU_OK)
            SessionDoorbell(ev.bits);

        SocketPump();

        /* 4. THEN fire expired timers */
        TimerRunExpired();
        SessionReapDead();

        /* 5. legacy pollers, still bounded by the cap above */
        ArpTick();
        DhcpTick();
        DnsTick();
    }

    return 0;
}
