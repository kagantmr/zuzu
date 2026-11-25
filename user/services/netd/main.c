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

/**
 * UDP echo handler
 */
static void UdpEchoHandler(Ipv4Addr src_ip, NetPort src_port, NetPort dst_port, const uint8_t *data,
                           uint16_t len)
{
    LOG_DEBUG(LOG_TAG, "UDP packet, from: %u.%u.%u.%u:%d, to: %u.%u.%u.%u:%d", IP4(src_ip), src_port,
             IP4(netif.ip), dst_port);
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

    retval = RegisterService("/svc/netd", g_svc_port);
    if (retval < 0) {
        LOG_ERROR(LOG_TAG, "registration failed");
        return retval;
    }

    return retval;
}

/** @brief Publish @p req in the control block and tell the driver to apply it. */
static Err SetRxFilter(const NetdRxFilter *req)
{
    NicCntlBlock *cntl = netif.drv.cntl;
    for (uint32_t i = 0; i < req->mcast_count; i++)
        for (uint32_t b = 0; b < MAC_ADDR_LEN; b++)
            cntl->mcast[i][b] = req->mcast[i][b];
    cntl->mcast_count = req->mcast_count;
    cntl->flags = req->flags & (NIC_CNTL_PROMISC | NIC_CNTL_ALLMULTI);
    return Signal(netif.drv.doorbell, NIC_DOORBELL_CNTL, false);
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

        NetdNetif out = {
            .ip = netif.ip, .netmask = netif.netmask, .gateway = netif.gateway, .dns = netif.dns};
        
        memcpy(out.mac, netif.mac, sizeof(out.mac));
        strncpy(out.name, netif.name, sizeof(out.name) - 1);
        const NicStatsBlock *stats = netif.drv.stats;
        const NicCntlBlock *cntl = netif.drv.cntl;
        if (stats) {
            out.link_up = !stats->link_down;
            for (int i = 0; i < NIC_STAT_COUNT; i++)
                out.stat[i] = stats->stat[i];
        }
        if (cntl) {
            out.rx_flags = cntl->flags;
            out.mcast_count = cntl->mcast_count > NIC_MCAST_MAX ? NIC_MCAST_MAX : cntl->mcast_count;
            for (uint32_t i = 0; i < out.mcast_count; i++)
                for (uint32_t b = 0; b < MAC_ADDR_LEN; b++)
                    out.mcast[i][b] = cntl->mcast[i][b];
        }
        MsgWrite(&out, sizeof(out));
        CtlReply(sizeof(out), -1);
    } break;
    case NETD_SET_RXFILTER: {
        if (call->granted >= 0)
            HandleClose(call->granted);

        Err rc = ZUZU_OK;
        NetdRxFilter req;
        if (!netif.drv.cntl || netif.drv.doorbell < 0) {
            rc = ERR_SYSDOWN;
        } else if (call->xlen < sizeof(req)) {
            rc = ERR_BADARG;
        } else {
            memcpy(&req, GetMessageBox(), sizeof(req));
            if (req.mcast_count > NIC_MCAST_MAX)
                rc = ERR_BADARG;
            else
                rc = SetRxFilter(&req);
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
        DhcpStart(nif);
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

int main()
{
    Err retval = ZUZU_OK;

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
