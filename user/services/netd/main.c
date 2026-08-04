//#include <stdio.h>

#include <zuzu/msg.h>
#include <zuzu/task.h>
#include <zuzu/umem.h>
#include <zuzu/channel.h>
#include <zuzu/protocols/devm.h>
#include <zuzu/protocols/nic.h>
#include <zuzu/service.h>
#include <zuzu/types.h>
#include <zuzu/log.h>

#include "common/globals.h"
#include "common/timer.h"
#include "common/netrand.h"
#include "link/eth.h"
#include "link/arp.h"
#include "transport/udp.h"
#include "transport/tcp.h"
#include "transport/port.h"

#include "app/dns.h"
#include "app/dhcp.h"

nic_ring_t *tx_ring, *rx_ring;
Handle nic_port;
Handle nic_ntfn;
Handle tx_doorbell;
Handle netd_handles[2];
netif_t netif; /* filled at startup (htonl isn't constant); DHCP overwrites later */

#define LEGACY_POLL_CAP 50u

/**
 * UDP echo handler
 */
static void udp_echo_handler(ipv4_addr_t src_ip, port_t src_port,
                             port_t dst_port, const uint8_t *data, uint16_t len)
{
    LOG_INFO(LOG_TAG, "UDP packet, from: %u.%u.%u.%u:%d, to: %u.%u.%u.%u:%d",
             IP4(src_ip), src_port, IP4(netif.ip), dst_port);
    udp_tx(src_ip, dst_port, src_port, data, len);
}

static __attribute__((cold)) void on_resolved(const char *name, ipv4_addr_t ip, int status) {
    if (status == ZUZU_OK) {
        LOG_INFO(LOG_TAG, "%s -> %u.%u.%u.%u", name, IP4(ip));
        //tcp_connect(ip, 80);
        int s = tcp_listen(80);
        LOG_INFO(LOG_TAG, "listening on :80 (slot %d)", s);
    } else {
        LOG_INFO(LOG_TAG, "%s failed: %d", name, status);
    }
}

/* Fires once the lease is first acquired: the network is now usable. */
static __attribute__((cold)) void on_dhcp_bound(void) {
    LOG_INFO(LOG_TAG, "network up: ip %u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u",
             IP4(netif.ip), IP4(netif.gateway), IP4(netif.dns));
    dns_query("google.com", on_resolved);   /* smoke test now that we have DNS */
    
}

__attribute__((cold)) int get_shm() {
    Handle port = register_service("netd");
    if (port < 0) {
        LOG_ERROR(LOG_TAG, "registration failed");
        return ERR_SYSDOWN;
    }

    nic_port = lookup_service("nic0");
    if (nic_port < 0) {
        LOG_ERROR(LOG_TAG, "couldn't find nic0");
        return ERR_NOENT;
    }
    LOG_INFO(LOG_TAG, "nic0 port=%d", nic_port);

    // w1 = status (ZUZU_OK / -err); w2 = mac_lo, w3 = mac_hi carry the MAC bytes
    Message r = ZuzuMsgCall(nic_port, NIC_CMD_GETMAC, 0, 0);
    if ((int32_t)r.w1 != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "GETMAC failed");
        return 1;
    }
    /* L3 config (ip/netmask/gateway/dns) stays zero until DHCP binds it. */
    netif.mac[0] = (r.w2 >>  0) & 0xff;
    netif.mac[1] = (r.w2 >>  8) & 0xff;
    netif.mac[2] = (r.w2 >> 16) & 0xff;
    netif.mac[3] = (r.w2 >> 24) & 0xff;
    netif.mac[4] = (r.w3 >>  0) & 0xff;
    netif.mac[5] = (r.w3 >>  8) & 0xff;
    LOG_INFO(LOG_TAG, "MAC %02x:%02x:%02x:%02x:%02x:%02x",
             netif.mac[0], netif.mac[1], netif.mac[2],
             netif.mac[3], netif.mac[4], netif.mac[5]);

    // w1 = shmem handle, w2 = rx doorbell, w3 = tx doorbell (all >= 0 on success)
    r = ZuzuMsgCall(nic_port, NIC_CMD_GETBUF, 0, 0);
    if ((int32_t)r.w0 != 0 || (int32_t)r.w1 < 0 ||
        (int32_t)r.w2 < 0 || (int32_t)r.w3 < 0) {
        LOG_ERROR(LOG_TAG, "NIC_GETBUF failed");
        return 1;
    }

    void *addr = ZuzuMemMap((int32_t)r.w1, 0, PROT_RW, 0);
    if (ZuzuPtrIsErr(addr)) {
        LOG_ERROR(LOG_TAG, "shmem attach failed");
        return ERR_SYSDOWN;
    }
    LOG_INFO(LOG_TAG, "Address of shmem: %p", (VirtAddr)addr);

    rx_ring = (nic_ring_t *)((uint8_t *)addr + NIC_RX_OFFSET);
    tx_ring = (nic_ring_t *)((uint8_t *)addr + NIC_TX_OFFSET);
    
    netd_port = port;
    nic_ntfn = (Handle)r.w2;
    tx_doorbell = (Handle)r.w3;
    netd_handles[1] = nic_ntfn;
    LOG_INFO(LOG_TAG, "service port=%d nic_ntfn=%d tx_doorbell=%d nic_port=%d",
             netd_port, nic_ntfn, tx_doorbell, nic_port);
    return ZUZU_OK;
}

int main() {
    if (get_shm() < 0) {
        return ERR_SYSDOWN;
    }

    timer_init();
    netrand_init();
    
    arp_init();
    udp_init();
    port_init();
    dns_init();
    dhcp_init(on_dhcp_bound);   /* kicks off DORA; on_dhcp_bound fires when bound */

    udp_bind(7, udp_echo_handler);

    LOG_INFO(LOG_TAG, "online");
    
    while (1) {
        /* 1. size the sleep: until the next timer, but never longer than the
            cap, so the legacy pollers still get serviced. */
        uint32_t now  = net_now_ms();
        uint32_t next = timer_next_deadline();
        uint32_t sleep_ms;
        if (next == TIMER_NO_DEADLINE)
            sleep_ms = LEGACY_POLL_CAP;
        else if ((int32_t)(next - now) <= 0)
            sleep_ms = TIMEOUT_POLL;         /* already overdue: don't block */
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
        arp_tick(); dhcp_tick(); dns_tick();
    }

    return 0;
}
