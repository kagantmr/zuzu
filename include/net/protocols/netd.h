#ifndef NETD_PROTOCOL_H
#define NETD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    /* Interface management */
    NETD_GET_NETIF = 1,
    NETD_GET_ALL_IFS,
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
    NetdOpcode op;
    /* todo: arguments for socket open-close */
} NetdHandshakeData;

#define IP4(x)                                                                                     \
    (unsigned)((x) & 0xff), (unsigned)(((x) >> 8) & 0xff), (unsigned)(((x) >> 16) & 0xff),         \
        (unsigned)(((x) >> 24) & 0xff)

typedef uint32_t Ipv4Addr;
typedef uint16_t NetPort;
typedef uint8_t MacAddr[6];

/* One network interface's L2/L3 config. */
typedef struct {
    Ipv4Addr ip;
    Ipv4Addr netmask;
    Ipv4Addr gateway;
    Ipv4Addr dns;
    MacAddr mac;
    char name[8];
} NetdNetif;


#ifdef __cplusplus
}
#endif

#endif // NETD_PROTOCOL_H