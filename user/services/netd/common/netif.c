#include "netif.h"

Netif netifs[NETIF_MAX] = {[0 ... NETIF_MAX - 1].drv.doorbell = -1};
