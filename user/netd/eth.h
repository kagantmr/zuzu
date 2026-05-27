#ifndef NETD_ETHERNET_H
#define NETD_ETHERNET_H

#include "globals.h"
#include <zuzu/types.h>
#include <convert.h>
#include <zuzu/packetring.h>

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP 0x0800

/* NIC should already remove the other fields */
typedef struct __attribute__((packed)){
    mac_addr_t dst_mac;
    mac_addr_t src_mac;
    uint16_t ethertype;
} eth_hdr_t;

int eth_rx(uint8_t *data, uint16_t len);
int eth_tx(mac_addr_t dst_mac, uint16_t ethertype, uint8_t *payload, uint16_t len);

#endif /* NETD_ETHERNET_H */