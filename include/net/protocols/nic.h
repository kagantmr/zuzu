#ifndef NIC_PROTOCOL_H
#define NIC_PROTOCOL_H

#include <net/packetring.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// should be 1 byte
/* bits on the doorbell event shared by the driver and netd */
#define NIC_DOORBELL_TX (1u << 0) /* netd -> driver: frames queued in tx ring */
#define NIC_DOORBELL_RX (1u << 1) /* driver -> netd: frames queued in rx ring */

typedef enum {
    NIC_STAT_IRQ = 0,      // interrupts serviced
    NIC_STAT_RX_PACKETS,   // frames delivered to the rx ring
    NIC_STAT_TX_PACKETS,   // frames written to the tx FIFO
    NIC_STAT_RX_RING_FULL, // rx ring full -> frame dropped
    NIC_STAT_RX_ERRORS,    // NIC-flagged bad rx frames
    NIC_STAT_RX_OVERSIZE,  // rx pkt_len > NIC_FRAME_SIZE -> dropped
    NIC_STAT_TX_DROPS,     // tx FIFO full -> frame dropped
    NIC_STAT_COUNT
} NicStat;

typedef struct {
    volatile uint32_t stat[NIC_STAT_COUNT];
} NicStatsBlock;

#define NIC_STATS_OFFSET (NIC_RX_OFFSET + ((NIC_RING_BYTES + 7u) & ~7u))

_Static_assert(NIC_STATS_OFFSET + sizeof(NicStatsBlock) <= NIC_RX_OFFSET + NIC_RING_STRIDE,
               "stats overflow rx page group");

#ifdef __cplusplus
}
#endif

#endif // NIC_PROTOCOL_H
