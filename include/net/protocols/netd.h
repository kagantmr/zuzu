#ifndef NETD_PROTOCOL_H
#define NETD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    /* Interface management */
    NETD_GET_NETIF = 1,
    NETD_DNS_RESOLVE,
    NETD_DRVHANDSHAKE_INIT, /* Used by NIC driver to hand the doorbell over to netd */
    NETD_DRVHANDSHAKE_STAGE2, /* hands over TX shm */
    NETD_DRVHANDSHAKE_STAGE3, /* hands over RX shm */
    NETD_DRVNOTIFY,
    NETD_DRVWATCH,

    /* UDP operations */
    SOCKET_UDP,
    SOCKET_UDP_RECV,
    SOCKET_UDP_SENDTO,

    /* TCP operations */
    SOCKET_TCP,
    SOCKET_TCP_RECV,
    SOCKET_TCP_SEND,
    SOCKET_TCP_ACCEPT,

    /* Connection & Lifecycle Management */
    SOCKET_BIND,
    SOCKET_CONNECT,
    SOCKET_LISTEN,
    SOCKET_CLOSE,

    /* lower-level Sockets */
    SOCKET_IP,
    SOCKET_FRAME,

    NETD_OPCODE_COUNT
} NetdOpcode;

typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint8_t mac[6];
    char name[8];
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_ring_full;
    uint32_t rx_errors;
    uint32_t tx_drops;
} NetdNetif;

typedef struct {
    NetdOpcode op;
    /* todo: arguments for socket open-close */
} NetdHandshakeData;

#ifdef __cplusplus
}
#endif

#endif // NETD_PROTOCOL_H