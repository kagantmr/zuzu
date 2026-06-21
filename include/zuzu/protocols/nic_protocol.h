#ifndef NIC_PROTOCOL_H
#define NIC_PROTOCOL_H

#define NIC_CMD_SEND 1
#define NIC_CMD_GETMAC 2
#define NIC_CMD_GETBUF 3
#define NIC_CMD_STATS 4    // arg (r3) = NIC_STAT_* index; reply r2 = value, r3 = NIC_STAT_COUNT
#define NIC_CMD_RECV 5     // blocks until next packet lands in shmem; reply r2 = packet length

/* Counters exposed by NIC_CMD_STATS. Call once per index (0..NIC_STAT_COUNT-1);
   the reply also returns NIC_STAT_COUNT in r3 so callers can iterate. */
enum nic_stat {
    NIC_STAT_IRQ = 0,      // interrupts serviced
    NIC_STAT_RX_PACKETS,   // frames delivered to the rx ring
    NIC_STAT_TX_PACKETS,   // frames written to the tx FIFO
    NIC_STAT_RX_RING_FULL, // rx ring full -> frame dropped
    NIC_STAT_RX_ERRORS,    // NIC-flagged bad rx frames
    NIC_STAT_RX_OVERSIZE,  // rx pkt_len > NIC_FRAME_SIZE -> dropped
    NIC_STAT_TX_DROPS,     // tx FIFO full -> frame dropped
    NIC_STAT_COUNT
};

// zuzu error types...

#endif // NIC_PROTOCOL_H
