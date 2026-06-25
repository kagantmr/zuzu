#include <stdio.h>

#include <zuzu/ipc.h>
#include <zuzu/task.h>
#include <zuzu/umem.h>
#include <zuzu/channel.h>
#include <zuzu/protocols/devmgr_protocol.h>
#include <zuzu/protocols/nic_protocol.h>
#include <zuzu/service.h>
#include <zuzu/types.h>
#include <zuzu/log.h>

#include "globals.h"
#include "eth.h"
#include "arp.h"
#include "udp.h"

#include "dns.h"
#include "dhcp.h"

nic_ring_t *tx_ring, *rx_ring;
handle_t nic_port;
handle_t nic_ntfn;
handle_t tx_doorbell;
handle_t handles[2];
netif_t netif; /* filled at startup (htonl isn't constant); DHCP overwrites later */

#if ZUZU_LOG_LEVEL_DEBUG == LOG_LEVEL
static void dump_packet(const uint8_t *data, uint16_t len)
{
    uint16_t limit = len < 64 ? len : 64;
    LOG_DEBUG(LOG_TAG, "netd: packet dump len=%u", (unsigned)len);
    for (uint16_t i = 0; i < limit; i++) {
        if ((i % 16) == 0)
            printf("\n  %04u:", (unsigned)i);
        printf(" %02x", data[i]);
    }
    if (limit < len)
        printf(" ...");
    printf("\n");
}
#endif

static void udp_echo_handler(ipv4_addr_t src_ip, uint16_t src_port,
                             uint16_t dst_port, const uint8_t *data, uint16_t len)
{
    LOG_INFO(LOG_TAG, "UDP packet, from: %u.%u.%u.%u:%d, to: %u.%u.%u.%u:%d",
             IP4(src_ip), src_port, IP4(netif.ip), dst_port);
    udp_tx(src_ip, dst_port, src_port, data, len);
}

static void on_resolved(const char *name, ipv4_addr_t ip, int status) {
    if (status == ZUZU_OK)
        LOG_INFO(LOG_TAG, "%s -> %u.%u.%u.%u", name, IP4(ip));
    else
        LOG_INFO(LOG_TAG, "%s failed: %d", name, status);
}

/* Fires once the lease is first acquired: the network is now usable. */
static void on_dhcp_bound(void) {
    LOG_INFO(LOG_TAG, "network up: ip %u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u",
             IP4(netif.ip), IP4(netif.gateway), IP4(netif.dns));
    dns_query("google.com", on_resolved);   /* smoke test now that we have DNS */
}

int get_shm() {
    handle_t port = register_service("netd");
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

    // r1 = status (ZUZU_OK / -err); r2 = mac_lo, r3 = mac_hi carry the MAC bytes
    msg_t r = _call(nic_port, NIC_CMD_GETMAC, 0, 0);
    if ((int32_t)r.r1 != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "GETMAC failed");
        return 1;
    }
    /* L3 config (ip/netmask/gateway/dns) stays zero until DHCP binds it. */
    netif.mac[0] = (r.r2 >>  0) & 0xff;
    netif.mac[1] = (r.r2 >>  8) & 0xff;
    netif.mac[2] = (r.r2 >> 16) & 0xff;
    netif.mac[3] = (r.r2 >> 24) & 0xff;
    netif.mac[4] = (r.r3 >>  0) & 0xff;
    netif.mac[5] = (r.r3 >>  8) & 0xff;
    LOG_INFO(LOG_TAG, "MAC %02x:%02x:%02x:%02x:%02x:%02x",
             netif.mac[0], netif.mac[1], netif.mac[2],
             netif.mac[3], netif.mac[4], netif.mac[5]);

    // r1 = shmem handle, r2 = rx doorbell, r3 = tx doorbell (all >= 0 on success)
    r = _call(nic_port, NIC_CMD_GETBUF, 0, 0);
    if ((int32_t)r.r0 != 0 || (int32_t)r.r1 < 0 ||
        (int32_t)r.r2 < 0 || (int32_t)r.r3 < 0) {
        LOG_ERROR(LOG_TAG, "NIC_GETBUF failed");
        return 1;
    }

    void *addr = _attach((int32_t)r.r1);
    if (_ptr_is_err(addr)) {
        LOG_ERROR(LOG_TAG, "shmem attach failed");
        return ERR_SYSDOWN;
    }
    LOG_INFO(LOG_TAG, "Address of shmem: %p", (vaddr_t)addr);

    rx_ring = (nic_ring_t *)((uint8_t *)addr + NIC_RX_OFFSET);
    tx_ring = (nic_ring_t *)((uint8_t *)addr + NIC_TX_OFFSET);
    
    netd_port = port;
    nic_ntfn = (handle_t)r.r2;
    tx_doorbell = (handle_t)r.r3;
    handles[1] = nic_ntfn;
    LOG_INFO(LOG_TAG, "service port=%d nic_ntfn=%d tx_doorbell=%d nic_port=%d",
             netd_port, nic_ntfn, tx_doorbell, nic_port);
    return ZUZU_OK;
}


int main() {
    if (get_shm() < 0) {
        return ERR_SYSDOWN;
    }
    
    arp_init();
    udp_init();
    dns_init();
    dhcp_init(on_dhcp_bound);   /* kicks off DORA; on_dhcp_bound fires when bound */

    udp_bind(7, udp_echo_handler);

    LOG_INFO(LOG_TAG, "will start looping");
    while (1) {
        arp_tick(); /* drive ARP retransmits + cache aging every wake */
        dhcp_tick();
        dns_tick();

        recvany_result_t result;
        int32_t recv_rc = _recvany(handles, 2, 10, &result);
        if (recv_rc < 0) {
            continue;
        }

        switch (result.kind) {
            case RECVANY_KIND_NTFN: {
                nic_frame_t frame;
                while (packet_ring_pop(&frame, rx_ring) == 0) {
#if ZUZU_LOG_LEVEL_DEBUG == LOG_LEVEL
                    dump_packet(frame.data, frame.len);
#endif
                    eth_rx(frame.data, frame.len);
                }
                break;
            }
            case RECVANY_KIND_CALL: {
                /* netd exposes no request API yet. Reply so callers never block
                   forever and the reply capability is released. */
                _reply(result.source, ERR_NOMATCH, 0, 0);
                break;
            }
            case RECVANY_KIND_TIMEOUT: {
                break;
            }
        }
    }

    return 0;
}
