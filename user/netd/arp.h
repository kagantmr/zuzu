#ifndef NETD_ARP_H
#define NETD_ARP_H

#include "globals.h"
#include <stdint.h>
#include <vector.h>
#include <stdbool.h>

#define ARP_OPER_REQST 1
#define ARP_OPER_REPLY 2

typedef struct __attribute__((packed)) {
    uint16_t htype;      // hardware type: 1 for Ethernet
    uint16_t ptype;      // protocol type: 0x0800 for IPv4
    uint8_t  hlen;       // hardware address length: 6 for MAC
    uint8_t  plen;       // protocol address length: 4 for IPv4
    uint16_t oper;       // operation: 1 = request, 2 = reply
    uint8_t  sha[6];     // sender hardware address (MAC)
    uint8_t  spa[4];     // sender protocol address (IP)
    uint8_t  tha[6];     // target hardware address (MAC)
    uint8_t  tpa[4];     // target protocol address (IP)
} arp_packet_t;

typedef struct {
    ipv4_addr_t ip;
    uint8_t mac[6];
} arp_entry_t;

void arp_init();
void arp_learn(ipv4_addr_t ip, const uint8_t *mac_addr);
int arp_rx(uint8_t *data, uint16_t len);
int arp_request(ipv4_addr_t ip);
int arp_lookup(ipv4_addr_t ip, uint8_t *mac_out);

#endif /* NETD_ARP_H */