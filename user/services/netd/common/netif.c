#include "netif.h"

NetInterface netifs[NETIF_MAX] = {[0 ... NETIF_MAX - 1].drv.doorbell = -1};

void NetifInit(void)
{
    for (uint8_t i = 0; i < NETIF_MAX; i++)
        netifs[i].index = i;
}
