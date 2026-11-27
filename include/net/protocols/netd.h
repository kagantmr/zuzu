/**
 * @file
 * @brief netd's service protocol: driver handshake and network interface queries.
 */

#ifndef NETD_PROTOCOL_H
#define NETD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <net/nettypes.h>
#include <net/protocols/nic.h>
#include <stdint.h>

/** @brief Kind of interface a driver provides; netd names interfaces after it. */
typedef enum {
    NETD_IFTYPE_ETH = 0, /**< Wired Ethernet: eth0, eth1, ... */
    NETD_IFTYPE_WLAN,    /**< Wi-Fi: wlan0, wlan1, ... */
    NETD_IFTYPE_PWIRE,   /**< Pseudo wire between two netd instances: pwire0, ... */
    NETD_IFTYPE_COUNT    /**< Number of types. */
} NetdIfType;

/** @brief One-byte opcodes on netd's service port. */
typedef enum {
    NETD_GET_NETIF = 1,       /**< Read the interface configuration (NetdNetif). */
    NETD_DRVHANDSHAKE_INIT,   /**< Driver begins: grants its doorbell and sends its MAC, a u64
                                 driver key (0 = derive it from the MAC) and a NetdIfType byte. */
    NETD_DRVHANDSHAKE_STAGE2, /**< Grants the tx ring's memory. */
    NETD_DRVHANDSHAKE_STAGE3, /**< Grants the rx ring's memory; the reply holds the name and grants
                                 netd's event. */
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

    NETD_SET_RXFILTER, /**< Replace the interface's receive filter (NetdRxFilter); the reply is an
                          Err. */

    NETD_OPCODE_COUNT /**< Number of opcodes. */
} NetdOpcode;

/** @brief Request carrying a bare NetdOpcode; currently unused. */
typedef struct {
    NetdOpcode op; /**< The opcode. */
} NetdHandshakeData;

/** @brief Request for NETD_GET_NETIF: the first attached interface at slot @c index or later. */
typedef struct {
    uint8_t op;    /**< NETD_GET_NETIF. */
    uint8_t index; /**< First slot to look at; 0 starts the walk. */
} NetdGetNetif;

/** @brief One network interface's configuration, as NETD_GET_NETIF returns it. */
typedef struct {
    uint32_t index;   /**< Slot this interface is in; ask for index + 1 to continue the walk. */
    Ipv4Addr ip;      /**< Interface address. */
    Ipv4Addr netmask; /**< Subnet mask. */
    Ipv4Addr gateway; /**< Default gateway. */
    Ipv4Addr dns;     /**< DNS server. */
    MacAddr mac;      /**< Hardware address. */
    char name[8];     /**< Interface name, NUL-terminated. */
    uint32_t link_up; /**< Nonzero while the driver reports link. */
    uint32_t stat[NIC_STAT_COUNT]; /**< Driver counters, indexed by NicStat; 0 with no driver. */
    uint32_t rx_flags;    /**< Receive filter flags (NIC_CNTL_*) the driver was last given. */
    uint32_t mcast_count; /**< Number of valid entries in mcast. */
    MacAddr mcast[NIC_MCAST_MAX]; /**< Multicast addresses the driver was last given. */
} NetdNetif;

/** @brief Request for NETD_SET_RXFILTER: the whole receive filter, replacing the current one. */
typedef struct {
    uint8_t op;           /**< NETD_SET_RXFILTER. */
    uint8_t _pad[2];      /**< Zero, so the first word never reads as a SOCK_* command. */
    uint8_t index;        /**< Slot of the interface, as NetdNetif.index gives it. */
    uint32_t flags;       /**< NIC_CNTL_PROMISC and NIC_CNTL_ALLMULTI; other bits are ignored. */
    uint32_t mcast_count; /**< Number of entries in mcast, at most NIC_MCAST_MAX. */
    MacAddr mcast[NIC_MCAST_MAX]; /**< Multicast addresses to accept. */
} NetdRxFilter;

#ifdef __cplusplus
}
#endif

#endif // NETD_PROTOCOL_H