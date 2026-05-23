#ifndef NIC_PROTOCOL_H
#define NIC_PROTOCOL_H

#define NIC_CMD_SEND 1
#define NIC_CMD_GETMAC 2
#define NIC_CMD_GETBUF 3
#define NIC_CMD_STATS 4
#define NIC_CMD_RECV 5     // blocks until next packet lands in shmem; reply r2 = packet length

// zuzu error types...

#endif // NIC_PROTOCOL_H
