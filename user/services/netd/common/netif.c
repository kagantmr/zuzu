#include "netif.h"

NetInterface netifs[NETIF_MAX] = {[0 ... NETIF_MAX - 1].drv.doorbell = -1};

static NicRing lo_ring;
static NicStatsBlock lo_stats;
static NicCntlBlock lo_cntl;

NetInterface netif_lo = {
    .name = "lo",
    .index = NETIF_MAX,
    .link_up = true,
    .drv = {.tx_ring = &lo_ring,
            .rx_ring = &lo_ring,
            .stats = &lo_stats,
            .cntl = &lo_cntl,
            .doorbell = -1},
};

void NetifInit(void)
{
    for (uint8_t i = 0; i < NETIF_MAX; i++)
        netifs[i].index = i;
    netif_lo.ip = htonl(0x7F000001u);
    netif_lo.netmask = htonl(0xFF000000u);
}

NetInterface *NetifDefault(void)
{
    NetInterface *first = NULL;
    for (int i = 0; i < NETIF_MAX; i++) {
        NetInterface *n = &netifs[i];
        if (!NetifAttached(n))
            continue;
        if (n->gateway)
            return n;
        if (!first)
            first = n;
    }
    return first ? first : &netifs[0];
}

NetInterface *NetifRoute(Ipv4Addr dst)
{
    if (Ipv4IsLoopback(dst) || NetifByIp(dst))
        return &netif_lo;
    NetInterface *dflt = NULL;
    for (int i = 0; i < NETIF_MAX; i++) {
        NetInterface *n = &netifs[i];
        if (!NetifAttached(n) || n->ip == 0)
            continue;
        if (dst != BROADCAST_IP && NetifOnLink(n, dst))
            return n;
        if (!dflt && n->gateway)
            dflt = n;
    }
    return dflt ? dflt : NetifDefault();
}

NetInterface *NetifByIp(Ipv4Addr ip)
{
    if (ip == 0)
        return NULL;
    for (int i = 0; i < NETIF_MAX; i++)
        if (NetifAttached(&netifs[i]) && netifs[i].ip == ip)
            return &netifs[i];
    return NULL;
}
