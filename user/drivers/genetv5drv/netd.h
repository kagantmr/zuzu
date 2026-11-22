#ifndef DRIVERS_GENETV5DRV_NETD_H
#define DRIVERS_GENETV5DRV_NETD_H

#include <types.h>

/** @brief Create the shared packet rings and register with netd over the shared event. */
Err ConnectToNetd(void);
/** @brief Unmap and close the packet rings and netd's doorbell, if they exist. */
void NetdDisconnect(void);
/** @brief Move up to the budget received frame into netd's RX ring and ring its doorbell. */
bool NetdRx(void);
/** @brief Hand queued frames from netd's TX ring to the hardware; ERR_BUSY if some had to wait. */
Err NetdTx(void);
/** @brief Publish g_genet.link_up to netd and ring its link doorbell. */
void NetdReportLink(void);
/** @brief Copy the driver counters into the stats block netd reads. */
void NetdSyncStats(void);
/** @brief Program the MAC address filter and promiscuous mode from netd's control block. */
void NetdApplyCntl(void);

#endif /* DRIVERS_GENETV5DRV_NETD_H */
