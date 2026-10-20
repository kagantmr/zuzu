/**
 * @file
 * @brief Socket client: the app side of the netd socket protocol.
 */

#include <net/socket.h>
#include <stdlib.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

/** @brief This app's connection to netd. */
typedef struct {
    Handle port;      /**< /svc/netd. */
    Handle live;      /**< Port this app owns; netd watches it to notice the app's death. */
    Handle doorbell;  /**< netd's event, send-only. */
    Handle event;     /**< This app's event; netd rings bit 0. */
    uint32_t bit;     /**< Bit to ring on @c doorbell. */
    uint32_t session; /**< Session id netd assigned. */
    bool attached;    /**< Whether Attach() has succeeded. */
} SockClient;

/** @brief One socket, indexed by its socket number. */
typedef struct {
    bool used;        /**< Slot holds an open socket. */
    SockShm *shm;     /**< Mapped shared memory. */
    Handle mem;       /**< The shared memory object. */
    uint32_t id;      /**< netd's id for the socket. */
    uint32_t tx_head; /**< Private tx write position; published after each write. */
    uint32_t rx_tail; /**< Private rx read position; published after each read. */
} SockConn;

/** @brief The netd connection, shared by all sockets. */
static SockClient g_client;
/** @brief Socket table; grows on demand. */
static SockConn *g_conns;
/** @brief Slots in g_conns. */
static int g_nconns;

/**
 * @brief Sends a socket command to netd and waits for its status.
 *
 * @param[in]  port    netd's port.
 * @param[in]  req     Request.
 * @param[in]  len     Request length.
 * @param[in]  grant   Handle to grant, or -1.
 * @param[out] granted Handle netd granted back; may be NULL.
 * @return netd's status, or the IPC error.
 */
static Err SockCall(Handle port, const void *req, uint32_t len, Handle grant, Handle *granted)
{
    return MsgCallStatus(port, req, len, grant, granted);
}

/**
 * @brief Attaches to netd once: hands it a liveness port and this app's event, and takes its
 * doorbell.
 *
 * @return ZUZU_OK, or an error code.
 * @retval ERR_SYSDOWN   /svc/netd is not registered.
 * @retval ERR_NOMEM     Out of handles.
 * @retval ERR_MALFORMED netd did not grant a doorbell.
 */
static Err Attach(void)
{
    if (g_client.attached)
        return ZUZU_OK;

    Handle netd = LookupService("/svc/netd");
    if (netd < 0)
        return ERR_SYSDOWN;

    SockClient c = {.port = netd, .doorbell = -1};
    c.live = CreatePort();
    c.event = CreateEvent();
    if (c.live < 0 || c.event < 0)
        return ERR_NOMEM;

    Handle watch;
    Err rc = HandleDup(c.live, PERM_WAIT | PERM_TXFR, MARKER_NONE, &watch);
    if (rc != ZUZU_OK)
        return rc;
    SockAttachRequest req = {.cmd = SOCK_ATTACH};
    Handle bell = -1;
    rc = SockCall(netd, &req, sizeof(req), watch, &bell);
    HandleClose(watch);
    if (rc == ZUZU_OK && bell < 0)
        rc = ERR_MALFORMED;
    if (rc != ZUZU_OK) {
        if (bell >= 0)
            HandleClose(bell);
        return rc;
    }
    SockAttachReply rep;
    memcpy(&rep, GetMessageBox(), sizeof(rep));
    c.doorbell = bell;
    c.bit = rep.bit;
    c.session = rep.session;

    Handle mine;
    rc = HandleDup(c.event, PERM_SEND | PERM_TXFR, MARKER_NONE, &mine);
    if (rc != ZUZU_OK)
        return rc;
    SockNotifyRequest note = {.cmd = SOCK_NOTIFY, .session = c.session, .bit = 0};
    rc = SockCall(netd, &note, sizeof(note), mine, NULL);
    HandleClose(mine);
    if (rc != ZUZU_OK)
        return rc;

    c.attached = true;
    g_client = c;
    return ZUZU_OK;
}

/**
 * @brief Looks up an open socket.
 *
 * @param[in] sock Socket number.
 * @return The socket, or NULL if @p sock is not open.
 */
static SockConn *Conn(int sock)
{
    if (sock < 0 || sock >= g_nconns || !g_conns[sock].used)
        return NULL;
    return &g_conns[sock];
}

/**
 * @brief Finds a free socket number, doubling the table when it is full. The slot is not marked
 * used.
 *
 * @return The socket number, or ERR_NOMEM.
 */
static int AllocSock(void)
{
    int sock = 0;
    while (sock < g_nconns && g_conns[sock].used)
        sock++;
    if (sock == g_nconns) {
        int grown = g_nconns ? g_nconns * 2 : 8;
        SockConn *table = realloc(g_conns, (size_t)grown * sizeof(SockConn));
        if (!table)
            return ERR_NOMEM;
        memset(table + g_nconns, 0, (size_t)(grown - g_nconns) * sizeof(SockConn));
        g_conns = table;
        g_nconns = grown;
    }
    memset(&g_conns[sock], 0, sizeof(SockConn));
    return sock;
}

/**
 * @brief Creates and maps a socket's shared memory.
 *
 * @param[in,out] s Socket to fill in.
 * @return ZUZU_OK, or the error from creating or mapping the memory.
 */
static Err MapMem(SockConn *s)
{
    s->mem = CreateMem(SOCK_MEM_PAGES, 0);
    if (s->mem < 0)
        return (Err)s->mem;
    void *va = MemMap(s->mem, 0, PROT_RW);
    if (PtrIsErr(va)) {
        HandleClose(s->mem);
        return (Err)va;
    }
    s->shm = (SockShm *)va;
    return ZUZU_OK;
}

/**
 * @brief Unmaps and closes a socket's shared memory.
 *
 * @param[in,out] s Socket.
 */
static void UnmapMem(SockConn *s)
{
    MemUnmap(s->shm);
    HandleClose(s->mem);
    s->shm = NULL;
}

/**
 * @brief Sends a command that grants netd a mappable, writable copy of a socket's memory.
 *
 * @param[in] req Request.
 * @param[in] len Request length.
 * @param[in] s   Socket whose memory to grant.
 * @return netd's status, or the IPC error.
 */
static Err CallWithMem(const void *req, uint32_t len, const SockConn *s)
{
    Handle page;
    Err rc = HandleDup(s->mem, PERM_MAP | PERM_WRITE | PERM_TXFR, MARKER_NONE, &page);
    if (rc != ZUZU_OK)
        return rc;
    rc = SockCall(g_client.port, req, len, page, NULL);
    HandleClose(page);
    return rc;
}

int SockOpen(SockType kind)
{
    Err rc = Attach();
    if (rc != ZUZU_OK)
        return rc;
    int sock = AllocSock();
    if (sock < 0)
        return sock;
    SockConn *s = &g_conns[sock];
    rc = MapMem(s);
    if (rc != ZUZU_OK)
        return rc;

    SockOpenRequest req = {.cmd = SOCK_OPEN, .session = g_client.session, .kind = kind};
    rc = CallWithMem(&req, sizeof(req), s);
    if (rc != ZUZU_OK) {
        UnmapMem(s);
        return rc;
    }
    SockOpenReply rep;
    memcpy(&rep, GetMessageBox(), sizeof(rep));
    s->id = rep.sock;
    s->used = true;
    return sock;
}

Err SockBind(int sock, uint16_t port)
{
    SockConn *s = Conn(sock);
    if (!s)
        return ERR_BADARG;
    SockRequest req = {.cmd = SOCK_BIND, .session = g_client.session, .sock = s->id, .a = port};
    return SockCall(g_client.port, &req, sizeof(req), -1, NULL);
}

Err SockSendTo(int sock, uint32_t ip, uint16_t port, const void *buf, uint16_t len)
{
    SockConn *s = Conn(sock);
    if (!s)
        return ERR_BADARG;
    if (len > SOCK_DGRAM_MAX)
        return ERR_OVERFLOW;
    if (s->shm->state == SOCK_STATE_CLOSED)
        return ERR_NOTCONN;
    uint32_t rec = (sizeof(SockDgramHdr) + len + 3u) & ~3u;
    uint32_t used = s->tx_head - ShmRingLoadTail(&s->shm->tx);
    if (used > SOCK_RING_SIZE || rec > SOCK_RING_SIZE - used)
        return ERR_BUFFULL;

    uint8_t *ring = SockTxData(s->shm);
    SockDgramHdr h = {.peer_ip = ip, .peer_port = port, .len = len};
    ShmRingWriteAt(ring, SOCK_RING_SIZE, s->tx_head, &h, sizeof(h));
    ShmRingWriteAt(ring, SOCK_RING_SIZE, s->tx_head + sizeof(h), buf, len);
    s->tx_head += rec;
    ShmRingPublishHead(&s->shm->tx, s->tx_head);
    Signal(g_client.doorbell, 1U << g_client.bit, false);
    return ZUZU_OK;
}

int SockRecvFrom(int sock, void *buf, uint16_t cap, uint32_t *ip, uint16_t *port, Duration timeout)
{
    SockConn *s = Conn(sock);
    if (!s)
        return ERR_BADARG;
    const uint8_t *ring = SockRxData(s->shm);
    for (;;) {
        uint32_t head = ShmRingLoadHead(&s->shm->rx);
        uint32_t avail = head - s->rx_tail;
        if (avail > SOCK_RING_SIZE) {
            s->rx_tail = head;
            return ERR_MALFORMED;
        }
        if (avail >= sizeof(SockDgramHdr)) {
            SockDgramHdr h;
            ShmRingReadAt(ring, SOCK_RING_SIZE, s->rx_tail, &h, sizeof(h));
            uint32_t rec = (sizeof(h) + h.len + 3u) & ~3u;
            if (h.len > SOCK_DGRAM_MAX || rec > avail) {
                s->rx_tail = head;
                ShmRingPublishTail(&s->shm->rx, head);
                return ERR_MALFORMED;
            }
            uint16_t n = h.len < cap ? h.len : cap;
            ShmRingReadAt(ring, SOCK_RING_SIZE, s->rx_tail + sizeof(h), buf, n);
            if (ip)
                *ip = h.peer_ip;
            if (port)
                *port = h.peer_port;
            s->rx_tail += rec;
            ShmRingPublishTail(&s->shm->rx, s->rx_tail);
            return n;
        }
        if (s->shm->state == SOCK_STATE_CLOSED)
            return ERR_NOTCONN;
        if (timeout == TIMEOUT_POLL)
            return ERR_TIMEOUT;
        EventWaitResult w = WaitOnEvent(g_client.event, timeout);
        if (w.status != ZUZU_OK)
            return w.status;
    }
}

Err SockClose(int sock)
{
    SockConn *s = Conn(sock);
    if (!s)
        return ERR_BADARG;
    SockRequest req = {.cmd = SOCK_CLOSE, .session = g_client.session, .sock = s->id};
    Err rc = SockCall(g_client.port, &req, sizeof(req), -1, NULL);
    MemUnmap(s->shm);
    HandleClose(s->mem);
    s->used = false;
    return rc;
}

Err SockConnect(int sock, uint32_t ip, uint16_t port, Duration timeout)
{
    SockConn *s = Conn(sock);
    if (!s)
        return ERR_BADARG;
    SockRequest req = {
        .cmd = SOCK_CONNECT, .session = g_client.session, .sock = s->id, .a = ip, .b = port};
    Err rc = SockCall(g_client.port, &req, sizeof(req), -1, NULL);
    if (rc != ZUZU_OK)
        return rc;
    for (;;) {
        uint32_t state = s->shm->state;
        if (state == SOCK_STATE_ESTABLISHED)
            return ZUZU_OK;
        if (state == SOCK_STATE_CLOSED)
            return s->shm->error ? (Err)s->shm->error : ERR_NOTCONN;
        if (timeout == TIMEOUT_POLL)
            return ERR_TIMEOUT;
        EventWaitResult w = WaitOnEvent(g_client.event, timeout);
        if (w.status != ZUZU_OK)
            return w.status;
    }
}

Err SockListen(int sock, uint16_t port, int backlog)
{
    SockConn *s = Conn(sock);
    if (!s)
        return ERR_BADARG;
    SockRequest req = {.cmd = SOCK_LISTEN,
                       .session = g_client.session,
                       .sock = s->id,
                       .a = port,
                       .b = (uint32_t)backlog};
    return SockCall(g_client.port, &req, sizeof(req), -1, NULL);
}

/** @brief Memory prepared for the next accepted connection, kept across SockAccept() retries. */
static SockConn g_spare;
/** @brief Whether g_spare holds mapped memory. */
static bool g_spare_ready;

int SockAccept(int sock, Duration timeout)
{
    SockConn *l = Conn(sock);
    if (!l)
        return ERR_BADARG;
    uint32_t listener_id = l->id;

    int nsock = AllocSock();
    if (nsock < 0)
        return nsock;
    if (!g_spare_ready) {
        memset(&g_spare, 0, sizeof(g_spare));
        Err mapped = MapMem(&g_spare);
        if (mapped != ZUZU_OK)
            return mapped;
        g_spare_ready = true;
    }

    for (;;) {
        SockRequest req = {.cmd = SOCK_ACCEPT, .session = g_client.session, .sock = listener_id};
        Err rc = CallWithMem(&req, sizeof(req), &g_spare);
        if (rc == ZUZU_OK)
            break;
        if (rc != ERR_BUFEMPTY)
            return rc;
        if (timeout == TIMEOUT_POLL)
            return ERR_TIMEOUT;
        EventWaitResult w = WaitOnEvent(g_client.event, timeout);
        if (w.status != ZUZU_OK)
            return w.status;
    }
    SockOpenReply rep;
    memcpy(&rep, GetMessageBox(), sizeof(rep));
    g_conns[nsock] = g_spare;
    g_conns[nsock].id = rep.sock;
    g_conns[nsock].used = true;
    g_spare_ready = false;
    return nsock;
}

Err SockWait(Duration timeout)
{
    if (!g_client.attached)
        return ERR_NOTCONN;
    EventWaitResult w = WaitOnEvent(g_client.event, timeout);
    return w.status;
}

int SockSend(int sock, const void *buf, uint32_t len)
{
    SockConn *s = Conn(sock);
    if (!s)
        return ERR_BADARG;
    uint8_t *ring = SockTxData(s->shm);
    const uint8_t *p = buf;
    uint32_t left = len;
    while (left) {
        if (s->shm->state == SOCK_STATE_CLOSED)
            return s->shm->error ? (int)s->shm->error : ERR_NOTCONN;
        uint32_t used = s->tx_head - ShmRingLoadTail(&s->shm->tx);
        if (used > SOCK_RING_SIZE)
            used = SOCK_RING_SIZE;
        uint32_t room = SOCK_RING_SIZE - used;
        if (room == 0) {
            EventWaitResult w = WaitOnEvent(g_client.event, TIMEOUT_INFINITE);
            if (w.status != ZUZU_OK)
                return w.status;
            continue;
        }
        uint32_t n = left < room ? left : room;
        ShmRingWriteAt(ring, SOCK_RING_SIZE, s->tx_head, p, n);
        s->tx_head += n;
        p += n;
        left -= n;
        ShmRingPublishHead(&s->shm->tx, s->tx_head);
        Signal(g_client.doorbell, 1U << g_client.bit, false);
    }
    return (int)len;
}

int SockRecv(int sock, void *buf, uint32_t cap, Duration timeout)
{
    SockConn *s = Conn(sock);
    if (!s)
        return ERR_BADARG;
    const uint8_t *ring = SockRxData(s->shm);
    for (;;) {
        uint32_t avail = ShmRingLoadHead(&s->shm->rx) - s->rx_tail;
        if (avail > SOCK_RING_SIZE)
            avail = SOCK_RING_SIZE;
        if (avail) {
            uint32_t n = avail < cap ? avail : cap;
            ShmRingReadAt(ring, SOCK_RING_SIZE, s->rx_tail, buf, n);
            s->rx_tail += n;
            ShmRingPublishTail(&s->shm->rx, s->rx_tail);
            Signal(g_client.doorbell, 1U << g_client.bit, false);
            return (int)n;
        }
        if (s->shm->flags & SOCK_F_EOF)
            return 0;
        if (s->shm->state == SOCK_STATE_CLOSED)
            return s->shm->error ? (int)s->shm->error : 0;
        if (timeout == TIMEOUT_POLL)
            return ERR_TIMEOUT;
        EventWaitResult w = WaitOnEvent(g_client.event, timeout);
        if (w.status != ZUZU_OK)
            return w.status;
    }
}
