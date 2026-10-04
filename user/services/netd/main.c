// #include <stdio.h>

#include <dev/protocols/devm.h>
#include <net/protocols/netd.h>
#include <net/protocols/nic.h>
#include <types.h>
#include <util/channel.h>
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
Handle g_svc_port = -1;
Handle g_nic_doorbell_ev = -1;
netif_t netif; /* filled at startup (htonl isn't constant); DHCP overwrites later */

#define LEGACY_POLL_CAP 50u

/**
 * UDP echo handler
 */
static void udp_echo_handler(ipv4_addr_t src_ip, NetPort src_port, NetPort dst_port,
                             const uint8_t *data, uint16_t len)
{
    LOG_INFO(LOG_TAG, "UDP packet, from: %u.%u.%u.%u:%d, to: %u.%u.%u.%u:%d", IP4(src_ip), src_port,
             IP4(netif.ip), dst_port);
    udp_tx(src_ip, dst_port, src_port, data, len);
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
    dns_query("google.com", on_resolved); /* smoke test now that we have DNS */
}

static Err InitNetdServices(void)
{

    Err retval = ZUZU_OK;

    g_svc_port = CreatePort();
    if (g_svc_port < 0) {
        return g_svc_port;
    }

    retval = RegisterService("/svc/netd", g_svc_port);
    if (retval < 0) {
        LOG_ERROR(LOG_TAG, "registration failed");
        return retval;
    }

    return retval;
}

/**
 * @brief Whenever a NIC sends NETD_DRVHANDSHAKE_INIT, does the 3-message handshake.
 */
static __attribute__((cold)) Err PerformDriverHandshake(void)
{
    // first handshake init from driver contains MAC address in the messagebox
    
    return ZUZU_OK;
}

static Err WaitForDriver(void)
{
    Err retval = ZUZU_OK;
    while (1) {
        PortWaitResult res = FormatToPortWait(WaitOn(g_svc_port, TIMEOUT_INFINITE));
        if (res.status != ZUZU_OK)
            return res.status;
        // see if we got a proper handshake initiation
        if (res.xlen >= 1 && *((uint8_t *)GetMessageBox()) == NETD_DRVHANDSHAKE_INIT) {
            g_nic_doorbell_ev = res.granted; // if we got the grant that means it succeeded
            retval = PerformDriverHandshake();
            if (retval < 0)
                return retval;
            break;
        }
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
    udp_init();
    port_init();
    dns_init();
    dhcp_init(on_dhcp_bound); /* kicks off DORA; on_dhcp_bound fires when bound */

    udp_bind(7, udp_echo_handler);

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

        /* 2. sleep until a packet arrives or the deadline elapses */
        WaitanyResult result;
        int32_t recv_rc = ZuzuWaitany(netd_handles, 2, sleep_ms, &result);

        /* 3. DRAIN RX FIRST: process inbound before any timer fires */
        if (recv_rc >= 0 && result.kind == WAITANY_KIND_NTFN) {
            nic_frame_t frame;
            while (packet_ring_pop(&frame, rx_ring) == 0)
                eth_rx(frame.data, frame.len);
        } else if (recv_rc >= 0 && result.kind == WAITANY_KIND_CALL) {
            ZuzuMsgReply(result.source, ERR_NOSYS, 0, 0);
        }

        /* 4. THEN fire expired timers */
        timer_run_expired();

        /* 5. legacy pollers, still bounded by the cap above */
        arp_tick();
        dhcp_tick();
        dns_tick();
    }

    return 0;
}
