/**
 * @file
 * @brief netd's service protocol: driver handshake and network interface queries.
 */

#ifndef NETD_PROTOCOL_H
#define NETD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief One-byte opcodes on netd's service port. */
typedef enum {
    NETD_GET_NETIF = 1,       /**< Read the interface configuration (NetdNetif). */
    NETD_DRVHANDSHAKE_INIT,   /**< Driver begins: grants its doorbell and sends its MAC. */
    NETD_DRVHANDSHAKE_STAGE2, /**< Grants the tx ring's memory. */
    NETD_DRVHANDSHAKE_STAGE3, /**< Grants the rx ring's memory; the reply holds the name. */
    NETD_DRVNOTIFY,           /**< Reserved; unused. */
    NETD_DRVWATCH,            /**< Reserved; unused. */

    SOCKET_UDP,        /**< Reserved; sockets use the SOCK_* commands in net/socket.h. */
    SOCKET_UDP_RECV,   /**< Reserved. */
    SOCKET_UDP_SENDTO, /**< Reserved. */

    SOCKET_TCP,        /**< Reserved. */
    SOCKET_TCP_RECV,   /**< Reserved. */
    SOCKET_TCP_SEND,   /**< Reserved. */
    SOCKET_TCP_ACCEPT, /**< Reserved. */

    SOCKET_BIND,    /**< Reserved. */
    SOCKET_CONNECT, /**< Reserved. */
    SOCKET_LISTEN,  /**< Reserved. */
    SOCKET_CLOSE,   /**< Reserved. */

    SOCKET_IP,    /**< Reserved: raw IP sockets. */
    SOCKET_FRAME, /**< Reserved: raw Ethernet sockets. */

    NETD_OPCODE_COUNT /**< Number of opcodes. */
} NetdOpcode;

/** @brief Request carrying a bare NetdOpcode; currently unused. */
typedef struct {
    NetdOpcode op; /**< The opcode. */
} NetdHandshakeData;

/**
 * @brief Expands an IPv4 address in network byte order into four printf arguments, for
 * "%u.%u.%u.%u".
 */
#define IP4(x)                                                                                     \
    (unsigned)((x) & 0xff), (unsigned)(((x) >> 8) & 0xff), (unsigned)(((x) >> 16) & 0xff),         \
        (unsigned)(((x) >> 24) & 0xff)

/** @brief IPv4 address in network byte order. */
typedef uint32_t Ipv4Addr;
/** @brief TCP or UDP port. */
typedef uint16_t NetPort;
/** @brief Ethernet MAC address. */
typedef uint8_t MacAddr[6];

/** @brief One network interface's configuration, as NETD_GET_NETIF returns it. */
typedef struct {
    Ipv4Addr ip;      /**< Interface address. */
    Ipv4Addr netmask; /**< Subnet mask. */
    Ipv4Addr gateway; /**< Default gateway. */
    Ipv4Addr dns;     /**< DNS server. */
    MacAddr mac;      /**< Hardware address. */
    char name[8];     /**< Interface name, NUL-terminated. */
} NetdNetif;


#ifdef __cplusplus
}
#endif

#endif // NETD_PROTOCOL_H