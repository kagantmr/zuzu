#ifndef NETD_GLOBALS_H
#define NETD_GLOBALS_H

#include <zuzu/packetring.h>
#include <zuzu/types.h>

#define ZUZU_IP ((ipv4_addr_t)htonl((192u << 24) | (168u << 16) | (1u << 8) | 15u)) // currently static, change later with DHCP
#define BROADCAST_MAC {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define BROADCAST_IP 0xFFFFFFFF

typedef uint32_t ipv4_addr_t;
typedef uint8_t mac_addr_t[6];

extern nic_ring_t *tx_ring, *rx_ring;
extern handle_t nic_port;
extern handle_t nic_ntfn;
extern handle_t handles[2];
extern mac_addr_t mac;

#define drv_port nic_port
#define netd_port handles[0]


#endif
