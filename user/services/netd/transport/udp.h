#ifndef UDP_H
#define UDP_H

#include "../common/globals.h"
#include <types.h>

typedef struct __attribute__((packed)) {
    NetPort src_port;
    NetPort dst_port;
    uint16_t length; // UDP header + data
    uint16_t checksum;
} UdpHeader;

_Static_assert(sizeof(UdpHeader) == 8, "UdpHdr size");

#define UDP_MAX_TABLE 64
#define UDP_MAX_PAYLOAD (NIC_MTU - 20 - 8) // 1472

typedef void (*UdpCallback)(Ipv4Addr src_ip, NetPort src_port, NetPort dst_port,
                            const uint8_t *data, uint16_t len);
void UdpInit(void);
int UdpBind(NetPort port, UdpCallback handler);
void UdpUnbind(NetPort port);
void UdpRecv(void *data, uint16_t len, Ipv4Addr src_ip, Ipv4Addr dst_ip);
int UdpSend(Ipv4Addr dst_ip, NetPort src_port, NetPort dst_port, const uint8_t *payload,
            uint16_t payload_len);

#endif