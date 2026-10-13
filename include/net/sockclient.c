#include <arch/barrier.h>
#include <net/protocols/socket.h>
#include <stdlib.h>
#include <string.h>
#include <util/msg.h>
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
    SvcResult r = Call(port, len, grant);
    if (granted)
        *granted = (Handle)r.r3;
    if (r.r0 != ZUZU_OK)
        return (Err)r.r0;
    Err status;
    if ((uint32_t)r.r1 < sizeof(status))
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

    SvcResult watch = HandleDuplicate(c.live, PERM_WAIT | PERM_TXFR, MARKER_NONE);
    if (watch.r0 != ZUZU_OK)
        return (Err)watch.r0;
    SockAttachRequest req = {.cmd = SOCK_ATTACH};
    Handle bell = -1;
    Err rc = SockCall(netd, &req, sizeof(req), (Handle)watch.r1, &bell);
    HandleClose((Handle)watch.r1);
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

    SvcResult mine = HandleDuplicate(c.event, PERM_SEND | PERM_TXFR, MARKER_NONE);
    if (mine.r0 != ZUZU_OK)
        return (Err)mine.r0;
    SockNotifyRequest note = {.cmd = SOCK_NOTIFY, .session = c.session, .bit = 0};
    rc = SockCall(netd, &note, sizeof(note), (Handle)mine.r1, NULL);
    HandleClose((Handle)mine.r1);
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

int SockOpen(uint32_t kind)
{
    Err rc = Attach();
    if (rc != ZUZU_OK)
        return rc;

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
    SockConn *s = &g_conns[sock];
    memset(s, 0, sizeof(*s));

    s->mem = CreateMem(SOCK_MEM_PAGES);
    if (s->mem < 0)
        return (Err)s->mem;
    void *va = MemMap(s->mem, 0, PROT_RW);
    if (PtrIsErr(va)) {
        HandleClose(s->mem);
        return (Err)va;
    }
    s->shm = (SockShm *)va;

    SvcResult page = HandleDuplicate(s->mem, PERM_MAP | PERM_TXFR, MARKER_NONE);
    if (page.r0 != ZUZU_OK) {
        MemUnmap(va);
        HandleClose(s->mem);
        return (Err)page.r0;
    }
    SockOpenRequest req = {.cmd = SOCK_OPEN, .session = g_client.session, .kind = kind};
    rc = SockCall(g_client.port, &req, sizeof(req), (Handle)page.r1, NULL);
    HandleClose((Handle)page.r1);
    if (rc != ZUZU_OK) {
        MemUnmap(va);
        HandleClose(s->mem);
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
        EventWaitResult w = FormatToEventWait(WaitOn(g_client.event, timeout));
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
