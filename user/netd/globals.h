#ifndef NETD_GLOBALS_H
#define NETD_GLOBALS_H

#include <zuzu/packetring.h>
#include <zuzu/types.h>

#define ZUZU_IP htonl((10 << 24) | (0 << 16) | (2 << 8) | 15) // currently static, change later with DHCP

extern nic_ring_t *tx_ring, *rx_ring;
extern handle_t nic_port;
extern handle_t nic_ntfn;
extern handle_t handles[2];
extern uint8_t mac[6];

#define drv_port nic_port
#define netd_port handles[0]


#endif