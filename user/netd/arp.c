#include "arp.h"
#include "eth.h"
#include "globals.h"
#include <stdio.h>
#include <vector.h>
#include <stdbool.h>

DEFINE_VEC(arp_entry, arp_entry_t);

arp_entry_vec_t v;

void arp_init() {
    arp_entry_vec_init(&v);
}

int arp_rx(uint8_t *data, uint16_t len) {
    uint32_t ip = ZUZU_IP;
    if (len < sizeof(arp_packet_t)) 
        return ERR_MALFORMED;
    arp_packet_t *pkt = (arp_packet_t *)data;
    uint16_t op = ntohs(pkt->oper);
    switch (op) {
        case (ARP_OPER_REQST): {
            if (memcmp(pkt->tpa, &ip, 4) == 0) {
                arp_packet_t pkt_out;
                pkt_out.htype = htons(1);
                pkt_out.ptype = htons(ETH_TYPE_IP);
                pkt_out.hlen  = 6;
                pkt_out.plen  = 4;
                pkt_out.oper  = htons(2);          // reply
                memcpy(pkt_out.sha, mac, 6);       // our MAC
                memcpy(pkt_out.spa, &ip, 4);       // our IP
                memcpy(pkt_out.tha, pkt->sha, 6);  // their MAC
                memcpy(pkt_out.tpa, pkt->spa, 4);  // their IP
                  eth_tx(pkt->sha, ETH_TYPE_ARP, (uint8_t *)&pkt_out, sizeof(arp_packet_t));
            }
            break;
        }
        case (ARP_OPER_REPLY): {
            bool matched = false;
            for (size_t i = 0; i < v.len; i++) {
                arp_entry_t* entry = arp_entry_vec_get(&v, (int)i);
                if (memcmp(pkt->spa, &entry->ip, 4) == 0) {
                    matched = true;
                    memcpy(entry->mac, pkt->sha, 6);
                }
            }
            if (!matched) {
                arp_entry_t new_entry;
                new_entry.ip = *(uint32_t*)pkt->spa;
                memcpy(new_entry.mac, pkt->sha, 6);
                arp_entry_vec_push(&v, &new_entry);
            }
            break;
        }
    }

    return ZUZU_OK;
}

int arp_request(uint32_t ip) {
    arp_packet_t pkt;
    pkt.htype = htons(1);
    pkt.ptype = htons(ETH_TYPE_IP);
    pkt.hlen = 6;
    pkt.plen = 4;
    uint32_t our_ip = ZUZU_IP;
    pkt.oper = htons(1); // request
    memcpy(pkt.sha, mac, 6);       // our MAC
    memcpy(pkt.spa, &our_ip, 4);       // our IP
    // broadcast frame, no need to set it
    memcpy(pkt.tpa, &ip, 4);  // their IP
    uint8_t dst_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    eth_tx(dst_mac, ETH_TYPE_ARP, (uint8_t *)&pkt, sizeof(arp_packet_t));
    return ZUZU_OK;
}

int arp_lookup(uint32_t ip, uint8_t *mac_out) {
    for (size_t i = 0; i < v.len; i++) {
        if (memcmp(&v.data[i].ip, &ip, 4) == 0) {
            memcpy(mac_out, v.data[i].mac, 6);
            return ZUZU_OK;
        }
    }
    return ERR_NOENT;
}