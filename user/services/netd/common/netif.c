#include "netif.h"

NetInterface netifs[NETIF_MAX] = {[0 ... NETIF_MAX - 1].drv.doorbell = -1};

void NetifInit(void)
{
    for (uint8_t i = 0; i < NETIF_MAX; i++)
        netifs[i].index = i;
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
