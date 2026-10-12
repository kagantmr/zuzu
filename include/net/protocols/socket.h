#ifndef SOCKET_PROTOCOL_H
#define SOCKET_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <types.h>
#include <util/shm_ring.h>
#include <zuzu/err.h>

/*
 * Socket protocol between an app and netd.
 *
 */

#define SOCK_RING_SIZE 8192u /* bytes per direction, power of two */

/* netd's service port also speaks the 1-byte NetdOpcode protocol, so socket
 * commands live above it. */
#define SOCK_CMD_BASE 0x100u
#define SOCK_IS_CMD(c) (((c) >> 8) == 1u)

#define SOCK_ATTACH (SOCK_CMD_BASE + 1) /* grant: liveness port (PERM_WAIT|PERM_TXFR). {cmd}; reply SockAttachReply, grants netd's doorbell */
#define SOCK_NOTIFY (SOCK_CMD_BASE + 2) /* grant: the app's event. {cmd, session, bit} */
#define SOCK_OPEN (SOCK_CMD_BASE + 3)   /* grant: the socket's memory object. {cmd, session, kind}; reply SockOpenReply */
#define SOCK_BIND (SOCK_CMD_BASE + 4)   /* {cmd, sock, port}; port 0 picks an ephemeral one */
#define SOCK_LISTEN (SOCK_CMD_BASE + 5) /* {cmd, sock, port, backlog}, TCP only */
#define SOCK_CONNECT (SOCK_CMD_BASE + 6) /* {cmd, sock, ip, port}; completes asynchronously */
#define SOCK_ACCEPT (SOCK_CMD_BASE + 7)  /* grant: memory object for the new socket. {cmd, sock}; ERR_BUFEMPTY if none pending */
#define SOCK_SETOPT (SOCK_CMD_BASE + 8)  /* {cmd, sock, opt, value} */
#define SOCK_CLOSE (SOCK_CMD_BASE + 9)   /* {cmd, sock} */

#define SOCK_KIND_TCP 1u
#define SOCK_KIND_UDP 2u

#define SOCK_OPT_NODELAY 1u

typedef enum {
    SOCK_STATE_OPEN = 0, /* created, not bound or connected */
    SOCK_STATE_BOUND,
    SOCK_STATE_LISTENING,
    SOCK_STATE_CONNECTING,
    SOCK_STATE_ESTABLISHED,
    SOCK_STATE_CLOSED, /* error holds why; ZUZU_OK for an orderly end */
} SockState;

#define SOCK_F_EOF (1u << 0)     /* peer closed; rx ring holds the last bytes */
#define SOCK_F_TX_KICK (1u << 1) /* netd drained tx and wants a doorbell on the next write */
#define SOCK_F_RX_KICK (1u << 2) /* rx was full; netd wants a doorbell after the app reads */

typedef struct {
    ShmRingHdr tx; /* app produces, netd consumes */
    ShmRingHdr rx; /* netd produces, app consumes */
    volatile uint32_t state;
    volatile int32_t error;
    volatile uint32_t flags;
    volatile uint32_t rx_dropped; /* UDP datagrams dropped on a full rx ring */
    volatile uint32_t accept_pending;
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
} SockShm;

#define SOCK_HDR_BYTES 256u
#define SOCK_MEM_BYTES (SOCK_HDR_BYTES + 2u * SOCK_RING_SIZE)
#define SOCK_MEM_PAGES ((SOCK_MEM_BYTES + 4095u) / 4096u)

_Static_assert(sizeof(SockShm) <= SOCK_HDR_BYTES, "SockShm must fit its header area");
_Static_assert((SOCK_RING_SIZE & (SOCK_RING_SIZE - 1)) == 0, "SOCK_RING_SIZE must be a power of two");

static inline uint8_t *SockTxData(SockShm *s) { return (uint8_t *)s + SOCK_HDR_BYTES; }
static inline uint8_t *SockRxData(SockShm *s)
{
    return (uint8_t *)s + SOCK_HDR_BYTES + SOCK_RING_SIZE;
}

/* UDP rings carry records: SockDgramHdr then len payload bytes, padded to 4. */
typedef struct {
    uint32_t peer_ip;
    uint16_t peer_port;
    uint16_t len;
} SockDgramHdr;

typedef struct {
    uint32_t cmd;
} SockAttachRequest;
typedef struct {
    Err status;
    uint32_t session;
    uint32_t bit; /* bit to ring on the doorbell */
} SockAttachReply;

typedef struct {
    uint32_t cmd;
    uint32_t session;
    uint32_t bit; /* bit netd rings on the app event */
} SockNotifyRequest;

typedef struct {
    uint32_t cmd;
    uint32_t session;
    uint32_t kind;
} SockOpenRequest;
typedef struct {
    Err status;
    uint32_t sock;
} SockOpenReply;

typedef struct {
    uint32_t cmd;
    uint32_t sock;
    uint32_t a; /* bind/listen: port, connect: ip, setopt: option */
    uint32_t b; /* listen: backlog, connect: port, setopt: value */
} SockRequest;


#ifdef __cplusplus
}
#endif

#endif
