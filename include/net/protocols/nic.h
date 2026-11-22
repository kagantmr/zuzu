/**
 * @file
 * @brief Shared memory and doorbell protocol between a NIC driver and netd.
 */

#ifndef NIC_PROTOCOL_H
#define NIC_PROTOCOL_H

#include <net/nettypes.h>
#include <net/packetring.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Doorbell bit, netd to driver indicating frames are queued in the tx ring. */
#define NIC_DOORBELL_TX (1u << 0)

/** @brief Indicates there is a new control message */
#define NIC_DOORBELL_CNTL (1u << 1)

/** @brief Bit on netd's event that the driver signals when rx frames are queued or link_down
 * changes. */
#define NIC_NETD_BIT 30
/** @brief Mask of NIC_NETD_BIT. */
#define NIC_NETD_DOORBELL (1u << NIC_NETD_BIT)
/** @brief Maximum multicast groups the NIC is allowed to have. */
#define NIC_MCAST_MAX 15

/**
 * @brief Turn on promiscuous mode.
 */
#define NIC_CNTL_PROMISC (1u << 0)

/**
 * @brief Receive all multicast packets.
 */
#define NIC_CNTL_ALLMULTI (1u << 1)

/**
 * @brief Offset of the NIC control block.
 */
#define NIC_CNTL_OFFSET (NIC_STATS_OFFSET + ((sizeof(NicStatsBlock) + 7u) & ~7u))

/** @brief Driver statistics counters. */
typedef enum {
    NIC_STAT_IRQ = 0,         /**< Interrupts serviced. */
    NIC_STAT_RX_PACKETS,      /**< Frames delivered to the rx ring. */
    NIC_STAT_TX_PACKETS,      /**< Frames the hardware finished sending. */
    NIC_STAT_RX_RING_FULL,    /**< Frames dropped because the rx ring was full. */
    NIC_STAT_RX_ERRORS,       /**< Received frames the NIC flagged as bad. */
    NIC_STAT_RX_OVERSIZE,     /**< Received frames dropped for exceeding NIC_FRAME_SIZE. */
    NIC_STAT_TX_DROPS,        /**< Frames dropped because the tx FIFO was full. */
    NIC_STAT_TX_TIMEOUTS,     /**< Times the driver found its tx ring stuck. */
    NIC_STAT_RX_OVERFLOWS,    /**< Times the NIC's rx buffer overflowed. */
    NIC_STAT_TX_UNDERRUNS,    /**< Times the NIC's tx buffer ran dry mid-frame. */
    NIC_STAT_IRQ_OTHER,       /**< Interrupts with a cause the driver does not handle. */
    NIC_STAT_MAC_RX_BYTES,    /**< Bytes the MAC received, from its MIB; wraps at 2^32. */
    NIC_STAT_MAC_TX_BYTES,    /**< Bytes the MAC sent, from its MIB; wraps at 2^32. */
    NIC_STAT_MAC_RX_FCS,      /**< Frames the MAC received with a bad FCS. */
    NIC_STAT_MAC_RX_ALIGN,    /**< Frames the MAC received with an alignment error. */
    NIC_STAT_MAC_RX_RUNT,     /**< Runt frames the MAC received. */
    NIC_STAT_MAC_RX_OVERSIZE, /**< Oversize frames the MAC received. */
    NIC_STAT_COUNT            /**< Number of counters. */
} NicStat;

/** @brief Counters the driver keeps in the shared memory for netd and tools to read. */
typedef struct {
    volatile uint32_t stat[NIC_STAT_COUNT]; /**< Counters, indexed by NicStat. */
    volatile uint32_t link_down; /**< Nonzero while the driver sees no link; 0 means up. */
} NicStatsBlock;

/** @brief Control settings netd can alter from the driver. */
typedef struct {
    volatile uint32_t flags;               /**< Flags of the network interface */
    volatile uint32_t mcast_count;         /**< Count of multicast entries. */
    volatile MacAddr mcast[NIC_MCAST_MAX]; /**< Multicast MAC addresses. */
} NicCntlBlock;

/** @brief Offset of the NicStatsBlock: in the rx ring's pages, just after the ring. */
#define NIC_STATS_OFFSET (NIC_RX_OFFSET + ((NIC_RING_BYTES + 7u) & ~7u))

_Static_assert(NIC_STATS_OFFSET + sizeof(NicStatsBlock) <= NIC_RX_OFFSET + NIC_RING_STRIDE,
               "stats overflow rx page group");
_Static_assert(NIC_CNTL_OFFSET + sizeof(NicCntlBlock) <= NIC_RX_OFFSET + NIC_RING_STRIDE,
               "cntl overflows rx page group");

#ifdef __cplusplus
}
#endif

#endif // NIC_PROTOCOL_H
