#ifndef NIC_PROTOCOL_H
#define NIC_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NIC_CMD_GETMAC, // 1 byte of cmd
    NIC_CMD_STATS, // 1 byte of cmd followed by 1 byte of the stat
    NIC_CMD_COUNT
} NicCommand;

// should be 1 byte
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

#ifdef __cplusplus
}
#endif

#endif // NIC_PROTOCOL_H
