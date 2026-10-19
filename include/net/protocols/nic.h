/**
 * @file
 * @brief Shared memory and doorbell protocol between a NIC driver and netd.
 */

#ifndef NIC_PROTOCOL_H
#define NIC_PROTOCOL_H

#include <net/packetring.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Doorbell bit, netd to driver: frames queued in the tx ring. */
#define NIC_DOORBELL_TX (1u << 0)
/** @brief Doorbell bit, driver to netd: frames queued in the rx ring. */
#define NIC_DOORBELL_RX (1u << 1)

/** @brief Driver statistics counters. */
typedef enum {
    NIC_STAT_IRQ = 0,      /**< Interrupts serviced. */
    NIC_STAT_RX_PACKETS,   /**< Frames delivered to the rx ring. */
    NIC_STAT_TX_PACKETS,   /**< Frames written to the hardware tx FIFO. */
    NIC_STAT_RX_RING_FULL, /**< Frames dropped because the rx ring was full. */
    NIC_STAT_RX_ERRORS,    /**< Received frames the NIC flagged as bad. */
    NIC_STAT_RX_OVERSIZE,  /**< Received frames dropped for exceeding NIC_FRAME_SIZE. */
    NIC_STAT_TX_DROPS,     /**< Frames dropped because the tx FIFO was full. */
    NIC_STAT_COUNT         /**< Number of counters. */
} NicStat;

/** @brief Counters the driver keeps in the shared memory for netd and tools to read. */
typedef struct {
    volatile uint32_t stat[NIC_STAT_COUNT]; /**< Counters, indexed by NicStat. */
} NicStatsBlock;

/** @brief Offset of the NicStatsBlock: in the rx ring's pages, just after the ring. */
#define NIC_STATS_OFFSET (NIC_RX_OFFSET + ((NIC_RING_BYTES + 7u) & ~7u))

_Static_assert(NIC_STATS_OFFSET + sizeof(NicStatsBlock) <= NIC_RX_OFFSET + NIC_RING_STRIDE,
               "stats overflow rx page group");

#ifdef __cplusplus
}
#endif

#endif // NIC_PROTOCOL_H
