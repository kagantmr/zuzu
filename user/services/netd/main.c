// #include <stdio.h>

#include <dev/protocols/devm.h>
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
#include "transport/tcp.h"
#include "transport/udp.h"

#include "app/dhcp.h"
#include "app/dns.h"

NicRing *tx_ring, *rx_ring;
NicStatsBlock *stats;
Handle g_svc_port = -1;
Handle g_event = -1;
Handle g_nic_doorbell_ev = -1;
netif_t netif; /* filled at startup (htonl isn't constant); DHCP overwrites later */

#define LEGACY_POLL_CAP 50u
#define LOOP_SLICE_MS 10u
#define PORT_BIT 0
#define PORT_MASK (1u << PORT_BIT)

/**
 * UDP echo handler
 */
static void udp_echo_handler(ipv4_addr_t src_ip, NetPort src_port, NetPort dst_port,
                             const uint8_t *data, uint16_t len)
{
    LOG_INFO(LOG_TAG, "UDP packet, from: %u.%u.%u.%u:%d, to: %u.%u.%u.%u:%d", IP4(src_ip), src_port,
             IP4(netif.ip), dst_port);
    UdpSend(src_ip, dst_port, src_port, data, len);
}

static __attribute__((cold)) void on_resolved(const char *name, ipv4_addr_t ip, int status)
{
    if (status == ZUZU_OK) {
        LOG_INFO(LOG_TAG, "%s -> %u.%u.%u.%u", name, IP4(ip));
        // tcp_connect(ip, 80);
        int s = tcp_listen(80);
        LOG_INFO(LOG_TAG, "listening on :80 (slot %d)", s);
    } else {
        LOG_INFO(LOG_TAG, "%s failed: %d", name, status);
    }
}

/* Fires once the lease is first acquired: the network is now usable. */
static __attribute__((cold)) void on_dhcp_bound(void)
{
    LOG_INFO(LOG_TAG, "network up: ip %u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u", IP4(netif.ip),
             IP4(netif.gateway), IP4(netif.dns));
    DnsQuery("google.com", on_resolved); /* smoke test now that we have DNS */
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

    retval = Bind(EVENT_PORT, g_event, g_svc_port, PORT_BIT);
    if (retval < 0) {
        LOG_ERROR(LOG_TAG, "port bind failed");
        return retval;
    }

    retval = RegisterService("/svc/netd", g_svc_port);
    if (retval < 0) {
        LOG_ERROR(LOG_TAG, "registration failed");
        return retval;
    }

    return retval;
}

static void RejectCall(Err err, Handle granted)
{
    if (granted >= 0)
        HandleClose(granted);
    MsgWrite(&err, sizeof(err));
    Reply(sizeof(err), -1);
}

static Err CheckStage(const PortWaitResult *r, uint8_t opcode, Marker drv_marker)
{
    if (r->xlen != 1 || *(uint8_t *)GetMessageBox() != opcode)
        return ERR_BADARG;
    if (r->granted < 0)
        return ERR_BADHANDLE;
    if (r->sender != drv_marker)
        return ERR_BADHANDLE;
    return ZUZU_OK;
}

/**
 * @brief Whenever a NIC sends NETD_DRVHANDSHAKE_INIT, does the 3-message handshake.
 */
static __attribute__((cold)) Err PerformDriverHandshake(PortWaitResult res)
{
    if (res.granted < 0) {
        RejectCall(ERR_BADHANDLE, -1);
        return ERR_BADHANDLE;
    }
    g_nic_doorbell_ev = res.granted;

    Marker drv_marker = res.sender;
    const uint8_t *msg = GetMessageBox();
    memcpy(&netif.mac[0], msg + 1, 4); // mac lo
    memcpy(&netif.mac[4], msg + 5, 2); // low 16 bits of mac hi

    Err rc = ZUZU_OK;
    MsgWrite(&rc, sizeof(rc));
    PortWaitResult r = FormatToPortWait(ReplyRecv(sizeof(rc), -1, g_svc_port, TIMEOUT_INFINITE));
    if (r.status != ZUZU_OK)
        return r.status;
    rc = CheckStage(&r, NETD_DRVHANDSHAKE_STAGE2, drv_marker);
    if (rc != ZUZU_OK) {
        RejectCall(rc, r.granted);
        return rc;
    }
    tx_ring = (NicRing *)MemMap(r.granted, 0, PROT_RW);
    if (PtrIsErr(tx_ring)) {
        RejectCall(ERR_BADHANDLE, r.granted);
        return ERR_BADHANDLE;
    }

    rc = ZUZU_OK;
    MsgWrite(&rc, sizeof(rc));
    r = FormatToPortWait(ReplyRecv(sizeof(rc), -1, g_svc_port, TIMEOUT_INFINITE));
    if (r.status != ZUZU_OK)
        return r.status;
    rc = CheckStage(&r, NETD_DRVHANDSHAKE_STAGE3, drv_marker);
    if (rc != ZUZU_OK) {
        RejectCall(rc, r.granted);
        return rc;
    }
    rx_ring = (NicRing *)MemMap(r.granted, 0, PROT_RW);
    if (PtrIsErr(rx_ring)) {
        RejectCall(ERR_BADHANDLE, r.granted);
        return ERR_BADHANDLE;
    }
    stats = (NicStatsBlock *)((uint8_t *)rx_ring + NIC_STATS_OFFSET);

    MsgWriter w;
    MsgWriterInit(&w);
    MsgPutU32(&w, ZUZU_OK);
    MsgPutStr(&w, "eth0");

    strncpy(netif.name, "eth0", 8);
    
    return Reply(w.off, -1);
}

static Err WaitForDriver(void)
{
    Err retval = ZUZU_OK;
    while (1) {
        PortWaitResult res = FormatToPortWait(WaitOn(g_svc_port, TIMEOUT_INFINITE));
        if (res.status != ZUZU_OK)
            return res.status;
        // see if we got a proper handshake initiation
        if (res.xlen >= 9 && *((uint8_t *)GetMessageBox()) == NETD_DRVHANDSHAKE_INIT) {
            retval = PerformDriverHandshake(res);
            if (retval < 0)
                return retval;
            break;
        }

        RejectCall(ERR_BADARG, res.granted);
    }
    return retval;
}

int main()
{
    Err retval = ZUZU_OK;

    retval = InitNetdServices();
    if (retval < 0)
        return retval;

    /* then wait for a driver to register, because without a driver netd wouldnt do anything */
    retval = WaitForDriver();
    if (retval < 0)
        return retval;

    timer_init();
    netrand_init();

    arp_init();
    UdpInit();
    port_init();
    DnsInit();
    dhcp_init(on_dhcp_bound); /* kicks off DORA; on_dhcp_bound fires when bound */

    UdpBind(7, udp_echo_handler);

    LOG_INFO(LOG_TAG, "online");

    while (1) {
        /* 1. size the sleep: until the next timer, but never longer than the
            cap, so the legacy pollers still get serviced. */
        uint32_t now = net_now_ms();
        uint32_t next = timer_next_deadline();
        uint32_t sleep_ms;
        if (next == TIMER_NO_DEADLINE)
            sleep_ms = LEGACY_POLL_CAP;
        else if ((int32_t)(next - now) <= 0)
            sleep_ms = TIMEOUT_POLL; /* already overdue: don't block */
        else
            sleep_ms = next - now > LEGACY_POLL_CAP ? LEGACY_POLL_CAP : next - now;

        /* 2. wait on the doorbell, then on the public port; a slice each so the
            timer deadline still holds */
        uint32_t slice = sleep_ms / 2;
        if (slice > LOOP_SLICE_MS)
            slice = LOOP_SLICE_MS;
        if (sleep_ms != TIMEOUT_POLL && slice == 0)
            slice = 1;

        WaitOn(g_nic_doorbell_ev, slice);

        /* 3. DRAIN RX FIRST: process inbound before any timer fires */
        NicFrame *frame;
        while ((frame = PacketRingPeek(rx_ring)) != NULL) {
            eth_rx(frame->data, (uint16_t)frame->len);
            PacketRingConsume(rx_ring);
        }

        EventWaitResult ev = FormatToEventWait(WaitOn(g_event, slice));
        if (ev.status == ZUZU_OK && (ev.bits & PORT_MASK)) {
            PortWaitResult call = FormatToPortWait(WaitOn(g_svc_port, TIMEOUT_POLL));
            NetdOpcode op;
            MsgRead(&op, sizeof(op));
            switch(op) {
                case NETD_GET_NETIF: {
                    MsgWrite(&netif, sizeof(netif));
                    Reply(sizeof(netif), (-1));
                } break;
                default: {
                    RejectCall(ERR_NOSYS, call.granted);
                    LOG_ERROR(LOG_TAG, "Unknown command"); 
                    continue;
                }
            }
            if (call.granted >= 0)
                HandleClose(call.granted);
            /* 
            while ((call = FormatToPortWait(WaitOn(g_svc_port, TIMEOUT_POLL))).status == ZUZU_OK)
                RejectCall(ERR_NOSYS, call.granted);*/
        }

        /* 4. THEN fire expired timers */
        timer_run_expired();

        /* 5. legacy pollers, still bounded by the cap above */
        arp_tick();
        dhcp_tick();
        DnsTick();
    }

    return 0;
}
