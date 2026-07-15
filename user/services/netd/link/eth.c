#include "eth.h"
#include <zuzu/msg.h>
#include <zuzu/ntfn.h>
#include <zuzu/protocols/nic_protocol.h>
#include <stdio.h>
#include <mem.h>
#include <stdlib.h>
#include "../common/globals.h"
#include "arp.h"
#include "../net/ip.h"

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
            if (len < sizeof(eth_hdr_t) + sizeof(ip_header_t)) {
                return ERR_MALFORMED;
            }
            ip_rx(data + 14, len - 14, hdr->src_mac);
            break;
        }
        default: {
            return ERR_MALFORMED;
        }
    }

    return ZUZU_OK;
}

int eth_tx(mac_addr_t dst_mac, uint16_t ethertype, uint8_t *payload, uint16_t len) {
    size_t total_len = sizeof(eth_hdr_t) + len;
    if (total_len > NIC_FRAME_SIZE)
        return ERR_OVERFLOW;

    /* Build the frame directly in a tx-ring slot */
    nic_frame_t *slot = packet_ring_reserve(tx_ring);
    if (!slot)
        return ERR_BUFFULL;

    eth_hdr_t *hdr = (eth_hdr_t *)slot->data;
    memcpy(hdr->dst_mac, dst_mac, 6);
    memcpy(hdr->src_mac, netif.mac, 6);
    hdr->ethertype = htons(ethertype);
    memcpy(slot->data + sizeof(eth_hdr_t), payload, len);
    slot->len = (uint32_t)total_len;
    packet_ring_commit(tx_ring);

    /* Async doorbell */
    return zuzu_ntfn_signal(tx_doorbell, 1);
}

int eth_send_frame(txframe_t *f, mac_addr_t dst_mac, uint16_t ethertype) {
    eth_hdr_t *hdr = (eth_hdr_t *)txframe_prepend(f, sizeof(eth_hdr_t));
    if (!hdr)
        return ERR_OVERFLOW; /* slot abandoned (uncommitted) */

    memcpy(hdr->dst_mac, dst_mac, 6);
    memcpy(hdr->src_mac, netif.mac, 6);
    hdr->ethertype = htons(ethertype);

    /* Header lands at slot->data[0], the whole frame is now contiguous. */
    f->slot->len = txframe_len(f);
    packet_ring_commit(tx_ring);
    return zuzu_ntfn_signal(tx_doorbell, 1);
}
