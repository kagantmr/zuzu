#include "eth.h"
#include <zuzu/ipc.h>
#include <zuzu/protocols/nic_protocol.h>
#include <mem.h>
#include <stdlib.h>
#include "globals.h"
#include "arp.h"
#include "ip.h"

int eth_rx(uint8_t *data, uint16_t len) {
    eth_hdr_t *hdr = (eth_hdr_t *)data;
    if (len < 14) {
        return ERR_MALFORMED;
    }
    uint16_t et = ntohs(hdr->ethertype);
    switch(et) {
        case ETH_TYPE_ARP: {
            arp_rx(data + 14, len - 14);
            break;
        }
        case ETH_TYPE_IP: {
            ip_rx(data + 14, len - 14);
            break;
        }
        default: {
            return ERR_MALFORMED;
        }
    }

    return ZUZU_OK;
}

int eth_tx(uint8_t *dst_mac, uint16_t ethertype, uint8_t *payload, uint16_t len) {
    eth_hdr_t hdr;
    memcpy(hdr.dst_mac, dst_mac, 6);
    memcpy(hdr.src_mac, mac, 6);
    hdr.ethertype = htons(ethertype);

    size_t total_len = sizeof(eth_hdr_t) + len;
    uint8_t *data = malloc(total_len);
    if (!data) return ERR_NOMEM;
    memcpy(data, &hdr, sizeof(eth_hdr_t));
    memcpy(data + sizeof(eth_hdr_t), payload, len);

    packet_ring_push(tx_ring, data, total_len);

    msg_t r = _call(drv_port, NIC_CMD_SEND, 0, 0);
    return (int)r.r3;
}