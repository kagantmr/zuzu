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

#define LOG_TAG "netd"

nic_ring_t *tx_ring, *rx_ring;
handle_t nic_port;
handle_t nic_ntfn;
handle_t handles[2];
mac_addr_t mac;

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

    // r2 = mac_lo on success, ERR_SYSDOWN (<0) on failure
    msg_t r = _call(nic_port, NIC_CMD_GETMAC, 0, 0);
    if ((int32_t)r.r2 < 0) {
        LOG_ERROR(LOG_TAG, "GETMAC failed");
        return 1;
    }
    mac[0] = (r.r2 >>  0) & 0xff;
    mac[1] = (r.r2 >>  8) & 0xff;
    mac[2] = (r.r2 >> 16) & 0xff;
    mac[3] = (r.r2 >> 24) & 0xff;
    mac[4] = (r.r3 >>  0) & 0xff;
    mac[5] = (r.r3 >>  8) & 0xff;
    LOG_INFO(LOG_TAG, "MAC %02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    r = _call(nic_port, NIC_CMD_GETBUF, 0, 0);
    if ((int32_t)r.r0 != 0 || (int32_t)r.r3 != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "NIC_GETBUF failed");
        return 1;
    }

    void *addr = _attach((int32_t)r.r1);
    if ((int)addr < 0) {
        LOG_ERROR(LOG_TAG, "shmem attach failed");
        return ERR_SYSDOWN;
    }
    LOG_INFO(LOG_TAG, "Address of shmem: %p", (vaddr_t)addr);

    rx_ring = (nic_ring_t *)addr;
    tx_ring = (nic_ring_t *)((uint8_t *)addr + 8192);

    netd_port = port;
    nic_ntfn = (handle_t)r.r2;
    handles[1] = nic_ntfn;
    LOG_INFO(LOG_TAG, "service port=%d nic_ntfn=%d nic_port=%d", netd_port, nic_ntfn, nic_port);
    return ZUZU_OK;
}

int main() {
    if (get_shm() < 0) {
        return ERR_SYSDOWN;
    }
    
    arp_init();

    LOG_INFO(LOG_TAG, "Probing gateway for ARP...");
    arp_request(htonl((192u << 24) | (168u << 16) | (1u << 8) | 1u)); // probe gateway 192.168.1.1

    LOG_INFO(LOG_TAG, "will start looping");
    while (1) {
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
                break;
            }
            case RECVANY_KIND_TIMEOUT: {
                break;
            }
        }
    }

    return 0;
}
