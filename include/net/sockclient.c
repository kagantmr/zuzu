#include <arch/barrier.h>
#include <net/protocols/socket.h>
#include <stdlib.h>
#include <string.h>
#include <ipc/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

typedef struct {
    Handle port;     /* /svc/netd */
    Handle live;     /* port we own; netd watches it to notice our death */
    Handle doorbell; /* send-only dup of netd's event */
    Handle event;    /* ours; netd rings bit 0 */
    uint32_t bit;    /* bit to ring on `doorbell` */
    uint32_t session;
    bool attached;
} SockClient;

typedef struct {
    bool used;
    SockShm *shm;
    Handle mem;
    uint32_t id;
    uint32_t tx_head;
    uint32_t rx_tail;
} SockConn;

static SockClient g_client;
static SockConn *g_conns;
static int g_nconns;

static Err SockCall(Handle port, const void *req, uint32_t len, Handle grant, Handle *granted)
{
    MsgWrite(req, len);
    uint32_t got = 0;
    Handle back = -1;
    Err rc = CallPort(port, len, grant, &got, &back);
    if (granted)
        *granted = back;
    if (rc != ZUZU_OK)
        return rc;
    Err status;
    if (got < sizeof(status))
        return ERR_MALFORMED;
    memcpy(&status, GetMessageBox(), sizeof(status));
    return status;
}

static void SockRingWrite(uint8_t *ring, uint32_t pos, const void *src, uint32_t n)
{
    uint32_t off = pos & (SOCK_RING_SIZE - 1);
    uint32_t first = n < SOCK_RING_SIZE - off ? n : SOCK_RING_SIZE - off;
    memcpy(ring + off, src, first);
    memcpy(ring, (const uint8_t *)src + first, n - first);
}

static void SockRingRead(const uint8_t *ring, uint32_t pos, void *dst, uint32_t n)
{
    uint32_t off = pos & (SOCK_RING_SIZE - 1);
    uint32_t first = n < SOCK_RING_SIZE - off ? n : SOCK_RING_SIZE - off;
    memcpy(dst, ring + off, first);
    memcpy((uint8_t *)dst + first, ring, n - first);
}

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

static SockConn *Conn(int sock)
{
    if (sock < 0 || sock >= g_nconns || !g_conns[sock].used)
        return NULL;
    return &g_conns[sock];
}

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

static void UnmapMem(SockConn *s)
{
    MemUnmap(s->shm);
    HandleClose(s->mem);
    s->shm = NULL;
}

static Err CallWithMem(const void *req, uint32_t len, const SockConn *s)
{
    Handle page;
    Err rc = HandleDup(s->mem, PERM_MAP | PERM_TXFR, MARKER_NONE, &page);
    if (rc != ZUZU_OK)
        return rc;
    rc = SockCall(g_client.port, req, len, page, NULL);
    HandleClose(page);
    return rc;
}

int SockOpen(uint32_t kind)
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
    uint32_t used = s->tx_head - s->shm->tx.tail;
    if (used > SOCK_RING_SIZE || rec > SOCK_RING_SIZE - used)
        return ERR_BUFFULL;

    uint8_t *ring = SockTxData(s->shm);
    SockDgramHdr h = {.peer_ip = ip, .peer_port = port, .len = len};
    SockRingWrite(ring, s->tx_head, &h, sizeof(h));
    SockRingWrite(ring, s->tx_head + sizeof(h), buf, len);
    s->tx_head += rec;
    ArchSyncBarrier();
    s->shm->tx.head = s->tx_head;
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
        uint32_t head = s->shm->rx.head;
        uint32_t avail = head - s->rx_tail;
        if (avail > SOCK_RING_SIZE) {
            s->rx_tail = head;
            return ERR_MALFORMED;
        }
        if (avail >= sizeof(SockDgramHdr)) {
            SockDgramHdr h;
            SockRingRead(ring, s->rx_tail, &h, sizeof(h));
            uint32_t rec = (sizeof(h) + h.len + 3u) & ~3u;
            if (h.len > SOCK_DGRAM_MAX || rec > avail) {
                s->rx_tail = head;
                s->shm->rx.tail = head;
                return ERR_MALFORMED;
            }
            uint16_t n = h.len < cap ? h.len : cap;
            SockRingRead(ring, s->rx_tail + sizeof(h), buf, n);
            if (ip)
                *ip = h.peer_ip;
            if (port)
                *port = h.peer_port;
            s->rx_tail += rec;
            s->shm->rx.tail = s->rx_tail;
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

static SockConn g_spare;
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
        uint32_t used = s->tx_head - s->shm->tx.tail;
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
        SockRingWrite(ring, s->tx_head, p, n);
        s->tx_head += n;
        p += n;
        left -= n;
        ArchSyncBarrier();
        s->shm->tx.head = s->tx_head;
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
        uint32_t avail = s->shm->rx.head - s->rx_tail;
        if (avail > SOCK_RING_SIZE)
            avail = SOCK_RING_SIZE;
        if (avail) {
            uint32_t n = avail < cap ? avail : cap;
            SockRingRead(ring, s->rx_tail, buf, n);
            s->rx_tail += n;
            ArchSyncBarrier();
            s->shm->rx.tail = s->rx_tail;
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
