/**
 * @file netif.h
 *
 * @brief Network interfaces: one per attached NIC driver.
 */

#ifndef NETD_NETIF_H
#define NETD_NETIF_H

#include "globals.h"
#include <net/packetring.h>
#include <net/protocols/nic.h>

/** @brief Number of interface slots. */
#define NETIF_MAX 4
/** @brief Size of Netif.name, matching NetdNetif.name. */
#define NETIF_NAME_MAX 8

/** @brief Shared memory and doorbell from the driver handshake. */
typedef struct {    
    NicRing *tx_ring;     /**< Frames netd produces for the driver. */
    NicRing *rx_ring;     /**< Frames the driver produces for netd. */
    NicStatsBlock *stats; /**< Driver counters and link state, inside the rx ring's MemObject. */
    NicCntlBlock *cntl;   /**< RX filter netd publishes to the driver, inside the rx ring's MemObject. */
    Handle doorbell;      /**< Driver's event: netd signals NIC_DOORBELL_* bits on it. */
} NetifDriver;

/** @brief One network interface. */
typedef struct {
    char name[NETIF_NAME_MAX]; /**< Interface name, e.g. "eth0". */
    uint8_t index;             /**< Slot in netifs[]. */
    MacAddr mac;               /**< Hardware address reported by the driver. */
    Ipv4Addr ip;               /**< IPv4 address, 0 until configured. */
    Ipv4Addr netmask;          /**< Subnet mask of ip. */
    Ipv4Addr gateway;          /**< Default gateway learned on this interface. */
    Ipv4Addr dns;              /**< Resolver learned on this interface. */
    bool link_up;              /**< Link state last seen in drv.stats. */
    NetifDriver drv;           /**< Driver link; doorbell is -1 while no driver is attached. */
} NetInterface;

/** @brief Interface table. */
extern NetInterface netifs[NETIF_MAX];

/** @brief Numbers every slot of netifs[]; call once at startup. */
void NetifInit(void);

/** @brief Default interface: the attached one with a gateway, else the first attached one. */
NetInterface *NetifDefault(void);

/** @brief The interface single-interface code uses until routing picks one per packet. */
#define netif (*NetifDefault())

/** @brief Whether a driver has attached to @p n. */
static inline bool NetifAttached(const NetInterface *n) { return n->drv.rx_ring != NULL; }

/** @brief Directed broadcast address of @p n's subnet. */
static inline Ipv4Addr NetifBroadcast(const NetInterface *n) { return n->ip | ~n->netmask; }

/** @brief Whether @p ip is on @p n's subnet. */
static inline bool NetifOnLink(const NetInterface *n, Ipv4Addr ip)
{
    return ((ip ^ n->ip) & n->netmask) == 0;
}

/** @brief Interface to send to @p dst on: the one whose subnet holds it, else the one with a gateway. */
NetInterface *NetifRoute(Ipv4Addr dst);

/** @brief The attached interface that owns address @p ip, or NULL. */
NetInterface *NetifByIp(Ipv4Addr ip);

#endif /* NETD_NETIF_H */
