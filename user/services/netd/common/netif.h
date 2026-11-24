#ifndef NETD_NETIF_H
#define NETD_NETIF_H

#include "globals.h"
#include "txframe.h"
#include <net/packetring.h>
#include <net/protocols/nic.h>

#define NETIF_MAX 4
#define NETIF_NAME_MAX 8 /* matches NetdNetif.name */
#define NETIF_MTU_ETHERNET 1500

typedef enum {
    NETIF_FREE,     /* slot unused */
    NETIF_DETACHED, /* driver gone: name and addresses kept, waiting for it to re-handshake */
    NETIF_UP,       /* driver attached, carries traffic */
} NetifState;

#define NETIF_F_LOOPBACK (1u << 0) /* frames sent are looped back to its own rx; no driver */
#define NETIF_F_FORWARD (1u << 1)  /* forward IPv4 packets that arrive here (router mode) */
#define NETIF_F_DHCP (1u << 2)     /* address comes from DHCP */

typedef struct Netif Netif;

/* What differs between a driver-backed interface and the loopback. */
typedef struct {
    int (*tx)(Netif *nif, TxFrame *f); /* hand one frame to the driver, or loop it back */
} NetifOps;

/* netd-side counters; the driver's own live in the NicStatsBlock. */
typedef struct {
    uint32_t rx_packets, rx_bytes, rx_dropped;
    uint32_t tx_packets, tx_bytes, tx_errors;
} NetifCounters;

struct Netif {
    /* identity */
    NetifState state;
    uint8_t index; /* slot in netifs[]; the key ARP entries and routes refer to */
    char name[NETIF_NAME_MAX]; /* "eth0", "lo": stable across a driver restart */
    uint32_t driver_key;       /* what the driver sent in its handshake; maps a restart to this slot */
    uint32_t flags;            /* NETIF_F_* */

    /* L2 */
    MacAddr mac;
    uint16_t mtu;

    /* L3: one IPv4 address per interface for now. The gateway is a route, not a field, and the
       resolver is netd-wide; both are filled in for NETD_GET_NETIF when queried. */
    Ipv4Addr ip;
    Ipv4Addr netmask;
    Ipv4Addr gateway;
    Ipv4Addr dns;

    /* driver link; all unused for the loopback */
    NicRing *tx_ring, *rx_ring;
    NicStatsBlock *stats;
    Handle doorbell;           /* event shared with the driver: RX bit in, TX bit out */
    uint8_t rx_bit, tx_bit;    /* this interface's bits in doorbell */

    const NetifOps *ops;
    struct NetifDhcp *dhcp; /* NULL unless NETIF_F_DHCP; defined in app/dhcp.c */
    NetifCounters counters;
};

extern Netif netifs[NETIF_MAX];

#define netif (netifs[0])

Netif *NetifAlloc(const char *name, uint32_t driver_key, uint32_t flags);
Netif *NetifByIndex(unsigned index);
Netif *NetifByName(const char *name);
Netif *NetifByKey(uint32_t driver_key);
Netif *NetifByIp(Ipv4Addr ip); /* the interface that owns this address, or NULL */

static inline Ipv4Addr NetifBroadcast(const Netif *n) { return n->ip | ~n->netmask; }
static inline bool NetifOnLink(const Netif *n, Ipv4Addr ip)
{
    return ((ip ^ n->ip) & n->netmask) == 0;
}

#endif