/**
 * @file
 * @brief Sockets: TCP and UDP through netd, over shared-memory rings.
 *
 * Each app attaches to netd once, lazily on its first SockOpen(). Every socket
 * is a memory object shared with netd that holds a SockShm header and one ring
 * per direction; data never goes through IPC. The app rings netd's doorbell
 * event after writing or reading, and netd signals the app's event when a
 * socket changes. netd notices the app's death through a port the app owns.
 *
 * Socket numbers are small integers local to the app, not handles.
 */

#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <types.h>
#include <zuzu/err.h>
#include <zuzu/shm_ring.h>

/** @brief Bytes in each ring, per direction; a power of two. */
#define SOCK_RING_SIZE 8192u

/**
 * @brief First socket command number.
 *
 * netd's service port also speaks the one-byte NetdOpcode protocol, so socket
 * commands live above it.
 */
#define SOCK_CMD_BASE 0x100u
/** @brief Tells whether a command number is a socket command. */
#define SOCK_IS_CMD(c) (((c) >> 8) == 1u)

/**
 * @brief Attach an app to netd. SockAttachRequest; grants the app's liveness port
 * (PERM_WAIT | PERM_TXFR). Replies SockAttachReply and grants netd's doorbell.
 */
#define SOCK_ATTACH (SOCK_CMD_BASE + 1)
/** @brief Give netd the app's event. SockNotifyRequest; grants the event. */
#define SOCK_NOTIFY (SOCK_CMD_BASE + 2)
/** @brief Open a socket. SockOpenRequest; grants its memory object. Replies SockOpenReply. */
#define SOCK_OPEN (SOCK_CMD_BASE + 3)
/**
 * @brief Bind a socket to a local port. SockRequest {sock, port}; port 0 picks an ephemeral one.
 */
#define SOCK_BIND (SOCK_CMD_BASE + 4)
/** @brief Listen on a port. SockRequest {sock, port, backlog}; TCP only. */
#define SOCK_LISTEN (SOCK_CMD_BASE + 5)
/** @brief Connect to a peer. SockRequest {sock, ip, port}; completes asynchronously. */
#define SOCK_CONNECT (SOCK_CMD_BASE + 6)
/**
 * @brief Take a pending connection. SockRequest {sock}; grants the new socket's memory
 * object. Replies SockOpenReply, or fails with ERR_BUFEMPTY if none is pending.
 */
#define SOCK_ACCEPT (SOCK_CMD_BASE + 7)
/** @brief Close a socket. SockRequest {sock}. */
#define SOCK_CLOSE (SOCK_CMD_BASE + 9)

/** @brief Socket kind. */
typedef enum {
    SOCK_KIND_TCP = 1U, /**< Stream socket. */
    SOCK_KIND_UDP = 2U  /**< Datagram socket. */
} SockType;

/** @brief Socket state, as netd publishes it in SockShm::state. */
typedef enum {
    SOCK_STATE_OPEN = 0,    /**< Created, not bound or connected. */
    SOCK_STATE_BOUND,       /**< Bound to a local port. */
    SOCK_STATE_LISTENING,   /**< Accepting connections. */
    SOCK_STATE_CONNECTING,  /**< Connect in progress. */
    SOCK_STATE_ESTABLISHED, /**< Connected. */
    SOCK_STATE_CLOSED,      /**< Finished; SockShm::error says why, ZUZU_OK for an orderly end. */
} SockState;

/** @brief SockShm::flags: the peer closed; the rx ring holds the last bytes. */
#define SOCK_F_EOF (1u << 0)
/** @brief SockShm::flags: netd drained tx and wants a doorbell on the next write. */
#define SOCK_F_TX_KICK (1u << 1)
/** @brief SockShm::flags: rx was full; netd wants a doorbell after the app reads. */
#define SOCK_F_RX_KICK (1u << 2)

/** @brief Header at the start of a socket's shared memory. */
typedef struct {
    ShmRingHdr tx;                 /**< App produces, netd consumes. */
    ShmRingHdr rx;                 /**< netd produces, app consumes. */
    volatile uint32_t state;       /**< A SockState. */
    volatile int32_t error;        /**< Why the socket closed, once state is SOCK_STATE_CLOSED. */
    volatile uint32_t flags;       /**< SOCK_F_* bits. */
    volatile uint32_t rx_dropped;  /**< UDP datagrams dropped on a full rx ring. */
    volatile uint32_t accept_pending; /**< Connections waiting for SockAccept() on a listener. */
    uint32_t local_ip;             /**< Local address. */
    uint32_t remote_ip;            /**< Peer address, once connected. */
    uint16_t local_port;           /**< Local port. */
    uint16_t remote_port;          /**< Peer port, once connected. */
} SockShm;

/** @brief Bytes reserved for SockShm before the rings. */
#define SOCK_HDR_BYTES 256u
/** @brief Size of a socket's shared memory: header, tx ring, rx ring. */
#define SOCK_MEM_BYTES (SOCK_HDR_BYTES + (2u * SOCK_RING_SIZE))
/** @brief Pages in a socket's memory object. */
#define SOCK_MEM_PAGES ((SOCK_MEM_BYTES + 4095u) / 4096u)

_Static_assert(sizeof(SockShm) <= SOCK_HDR_BYTES, "SockShm must fit its header area");
_Static_assert((SOCK_RING_SIZE & (SOCK_RING_SIZE - 1)) == 0,
               "SOCK_RING_SIZE must be a power of two");

/**
 * @brief Data area of a socket's tx ring.
 *
 * @param[in] s Socket header.
 * @return The first byte of the tx ring.
 */
static inline uint8_t *SockTxData(SockShm *s) { return (uint8_t *)s + SOCK_HDR_BYTES; }

/**
 * @brief Data area of a socket's rx ring.
 *
 * @param[in] s Socket header.
 * @return The first byte of the rx ring.
 */
static inline uint8_t *SockRxData(SockShm *s)
{
    return (uint8_t *)s + SOCK_HDR_BYTES + SOCK_RING_SIZE;
}

/** @brief Record header in a UDP ring; @c len payload bytes follow, padded to 4 bytes. */
typedef struct {
    uint32_t peer_ip;   /**< Sender (rx) or destination (tx) address. */
    uint16_t peer_port; /**< Sender or destination port. */
    uint16_t len;       /**< Payload length. */
} SockDgramHdr;

/** @brief SOCK_ATTACH request. */
typedef struct {
    uint32_t cmd; /**< SOCK_ATTACH. */
} SockAttachRequest;

/** @brief SOCK_ATTACH reply. */
typedef struct {
    Err status;       /**< ZUZU_OK, or why attaching failed. */
    uint32_t session; /**< Session id for later requests. */
    uint32_t bit;     /**< Bit to ring on netd's doorbell. */
} SockAttachReply;

/** @brief SOCK_NOTIFY request. */
typedef struct {
    uint32_t cmd;     /**< SOCK_NOTIFY. */
    uint32_t session; /**< Session from SockAttachReply. */
    uint32_t bit;     /**< Bit netd rings on the app's event. */
} SockNotifyRequest;

/** @brief SOCK_OPEN request. */
typedef struct {
    uint32_t cmd;     /**< SOCK_OPEN. */
    uint32_t session; /**< Session from SockAttachReply. */
    uint32_t kind;    /**< A SockType. */
} SockOpenRequest;

/** @brief SOCK_OPEN and SOCK_ACCEPT reply. */
typedef struct {
    Err status;    /**< ZUZU_OK, or why it failed. */
    uint32_t sock; /**< netd's id for the socket. */
} SockOpenReply;

/** @brief Request for the commands that act on an open socket. */
typedef struct {
    uint32_t cmd;     /**< SOCK_BIND, SOCK_LISTEN, SOCK_CONNECT, SOCK_ACCEPT or SOCK_CLOSE. */
    uint32_t session; /**< Session from SockAttachReply. */
    uint32_t sock;    /**< netd's socket id. */
    uint32_t a;       /**< First argument: port (bind, listen) or IP address (connect). */
    uint32_t b;       /**< Second argument: backlog (listen) or port (connect). */
} SockRequest;

/** @brief Largest UDP payload: a 1500-byte MTU minus IP and UDP headers. */
#define SOCK_DGRAM_MAX 1472u

/**
 * @brief Opens a socket, attaching to netd first if this is the app's first one.
 *
 * @param[in] kind SOCK_KIND_TCP or SOCK_KIND_UDP.
 * @return The socket number, or a negative error code.
 * @retval ERR_SYSDOWN /svc/netd is not registered.
 * @retval ERR_NOMEM   Out of memory or handles.
 */
int SockOpen(SockType kind);

/**
 * @brief Binds a socket to a local port.
 *
 * @param[in] sock Socket number.
 * @param[in] port Local port, or 0 for an ephemeral one.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADARG @p sock is not an open socket.
 */
Err SockBind(int sock, uint16_t port);

/**
 * @brief Queues one UDP datagram and rings netd. Does not block.
 *
 * @param[in] sock Socket number.
 * @param[in] ip   Destination address.
 * @param[in] port Destination port.
 * @param[in] buf  Payload.
 * @param[in] len  Payload length, at most SOCK_DGRAM_MAX.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADARG   @p sock is not an open socket.
 * @retval ERR_OVERFLOW @p len is larger than SOCK_DGRAM_MAX.
 * @retval ERR_NOTCONN  The socket is closed.
 * @retval ERR_BUFFULL  The tx ring has no room; try again later.
 */
Err SockSendTo(int sock, uint32_t ip, uint16_t port, const void *buf, uint16_t len);

/**
 * @brief Receives one UDP datagram.
 *
 * A datagram longer than @p cap is truncated and the rest discarded. While
 * nothing has arrived it waits for socket activity; each wait may last up to
 * @p timeout.
 *
 * @param[in]  sock    Socket number.
 * @param[out] buf     Payload.
 * @param[in]  cap     Size of @p buf.
 * @param[out] ip      Sender address; may be NULL.
 * @param[out] port    Sender port; may be NULL.
 * @param[in]  timeout Milliseconds, TIMEOUT_POLL or TIMEOUT_INFINITE.
 * @return Bytes copied into @p buf, or a negative error code.
 * @retval ERR_BADARG    @p sock is not an open socket.
 * @retval ERR_NOTCONN   The socket is closed.
 * @retval ERR_TIMEOUT   Nothing arrived in time.
 * @retval ERR_MALFORMED The rx ring was corrupt; it has been emptied.
 */
int SockRecvFrom(int sock, void *buf, uint16_t cap, uint32_t *ip, uint16_t *port, Duration timeout);

/**
 * @brief Closes a socket and releases its shared memory, even if netd reports an error.
 *
 * @param[in] sock Socket number.
 * @return netd's answer to the close.
 * @retval ERR_BADARG @p sock is not an open socket.
 */
Err SockClose(int sock);

/**
 * @brief Connects a TCP socket and waits until it is established or fails.
 *
 * @param[in] sock    Socket number.
 * @param[in] ip      Peer address.
 * @param[in] port    Peer port.
 * @param[in] timeout Limit on each wait for progress: milliseconds, TIMEOUT_POLL or
 *                    TIMEOUT_INFINITE.
 * @return ZUZU_OK once established, or an error code.
 * @retval ERR_BADARG  @p sock is not an open socket.
 * @retval ERR_TIMEOUT Still connecting.
 * @retval ERR_NOTCONN The connection failed without a more specific error.
 */
Err SockConnect(int sock, uint32_t ip, uint16_t port, Duration timeout);

/**
 * @brief Makes a TCP socket accept connections on a port.
 *
 * @param[in] sock    Socket number.
 * @param[in] port    Local port.
 * @param[in] backlog Connections netd queues before refusing more.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_BADARG @p sock is not an open socket.
 */
Err SockListen(int sock, uint16_t port, int backlog);

/**
 * @brief Takes the next connection from a listening socket.
 *
 * @param[in] sock    Listening socket number.
 * @param[in] timeout Limit on each wait for a connection: milliseconds, TIMEOUT_POLL or
 *                    TIMEOUT_INFINITE.
 * @return The new connection's socket number, or a negative error code.
 * @retval ERR_BADARG  @p sock is not an open socket.
 * @retval ERR_TIMEOUT No connection arrived in time.
 * @retval ERR_NOMEM   Out of memory or handles.
 */
int SockAccept(int sock, Duration timeout);

/**
 * @brief Writes all of @p buf to a TCP socket, blocking while the tx ring is full.
 *
 * @param[in] sock Socket number.
 * @param[in] buf  Data.
 * @param[in] len  Bytes to send.
 * @return @p len, or a negative error code.
 * @retval ERR_BADARG  @p sock is not an open socket.
 * @retval ERR_NOTCONN The socket closed without a more specific error.
 */
int SockSend(int sock, const void *buf, uint32_t len);

/**
 * @brief Reads whatever a TCP socket has, up to @p cap bytes, waiting if it has nothing.
 *
 * @param[in]  sock    Socket number.
 * @param[out] buf     Data.
 * @param[in]  cap     Size of @p buf.
 * @param[in]  timeout Limit on each wait for data: milliseconds, TIMEOUT_POLL or
 *                     TIMEOUT_INFINITE.
 * @return Bytes read, 0 at end of stream, or a negative error code.
 * @retval ERR_BADARG  @p sock is not an open socket.
 * @retval ERR_TIMEOUT No data arrived in time.
 */
int SockRecv(int sock, void *buf, uint32_t cap, Duration timeout);

/**
 * @brief Waits until netd signals activity on any of the app's sockets.
 *
 * @param[in] timeout Milliseconds, TIMEOUT_POLL or TIMEOUT_INFINITE.
 * @return ZUZU_OK, or an error code.
 * @retval ERR_NOTCONN No socket has been opened yet.
 * @retval ERR_TIMEOUT Nothing happened in time.
 */
Err SockWait(Duration timeout);

#ifdef __cplusplus
}
#endif

#endif
