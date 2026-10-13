#include "socket.h"
#include "port.h"
#include "session.h"
#include "tcp.h"
#include "tcp_pcb.h"
#include "udp.h"
#include <arch/barrier.h>
#include <malloc.h>
#include <string.h>
#include <util/log.h>
#include <zuzu/zuzu.h>

#define SOCK_HASH 32

static ListHead g_by_id[SOCK_HASH];
static ListHead g_by_port[SOCK_HASH];
static uint32_t g_next_id = 1;

static Socket *SockOf(int slot) { return tcp_get_ctx(slot); }

static void SocketTcpTx(Socket *s);

static void RingWrite(SockShm *shm, uint32_t pos, const void *src, uint32_t n)
{
    uint8_t *ring = SockRxData(shm);
    uint32_t off = pos & (SOCK_RING_SIZE - 1);
    uint32_t first = MIN(n, SOCK_RING_SIZE - off);
    memcpy(ring + off, src, first);
    memcpy(ring, (const uint8_t *)src + first, n - first);
}

static void SocketUdpRx(ipv4_addr_t src_ip, NetPort src_port, NetPort dst_port, const uint8_t *data,
                        uint16_t len)
{
    Socket *s = SocketByPort(dst_port);
    if (!s)
        return;
    if (s->shm->state == SOCK_STATE_CLOSED)
        return;
    size_t rec = (sizeof(SockDgramHdr) + len + 3) & ~3U;
    uint32_t tail = s->shm->rx.tail;
    uint32_t used = s->rx_head - tail;
    if (used > SOCK_RING_SIZE)
        used = SOCK_RING_SIZE; /* corrupted tail: treat as full */
    if (rec > SOCK_RING_SIZE - used) {
        s->shm->rx_dropped++;
        return;
    }
    SockDgramHdr h = {.peer_ip = src_ip, .peer_port = src_port, .len = len};
    RingWrite(s->shm, s->rx_head, &h, sizeof(h));
    RingWrite(s->shm, s->rx_head + sizeof(h), data, len);
    s->rx_head += rec;
    ArchSyncBarrier(); /* the data must be visible before the head moves */
    s->shm->rx.head = s->rx_head;
    SessionKick(s->session);
}

void SocketInit(void)
{
    for (int i = 0; i < SOCK_HASH; i++) {
        ListInit(&g_by_id[i]);
        ListInit(&g_by_port[i]);
    }
}

Err SocketOpen(int session, uint32_t kind, Handle mem, uint32_t *id)
{
    Err retval;
    if (kind != SOCK_KIND_UDP && kind != SOCK_KIND_TCP)
        return ERR_NOSYS;

    size_t mem_size;
    retval = HandleQuerySize(mem, &mem_size);
    if (retval != ZUZU_OK)
        return retval;
    if (mem_size < SOCK_MEM_BYTES)
        return ERR_BADARG;

    ListHead *sl = SessionSockets(session);
    if (!sl)
        return ERR_BADARG;

    if (SessionSocketsFull(session))
        return ERR_NOMEM;

    Socket *sock = calloc(1, sizeof(Socket));
    if (!sock)
        return ERR_NOMEM;
    sock->pcb = -1;

    sock->shm = (SockShm *)MemMap(mem, 0, PROT_RW);
    if (PtrIsErr(sock->shm)) {
        retval = (Err)sock->shm;
        free(sock);
        return retval;
    }
    memset(sock->shm, 0, sizeof(SockShm));
    ShmRingInit(&sock->shm->tx, SOCK_RING_SIZE);
    ShmRingInit(&sock->shm->rx, SOCK_RING_SIZE);
    sock->shm->state = SOCK_STATE_OPEN;
    sock->kind = kind;
    sock->mem = mem;
    sock->session = session;
    sock->id = g_next_id++;
    if (sock->id == 0)
        sock->id = g_next_id++;
    ListAddTail(&sock->id_node, &g_by_id[sock->id % SOCK_HASH].node);
    ListAddTail(&sock->sess_node, &sl->node);
    SessionSocketCount(session, +1);
    *id = sock->id;
    return ZUZU_OK;
}

Socket *SocketFor(uint32_t id, int session)
{
    ListNode *pos;
    LIST_FOR_EACH(pos, &g_by_id[id % SOCK_HASH].node)
    {
        Socket *s = container_of(pos, Socket, id_node);
        if (s->id == id && s->session == session)
            return s;
    }
    return NULL;
}

Err SocketBind(Socket *s, NetPort port)
{
    if (s->port != 0)
        return ERR_BUSY;
    if (s->kind != SOCK_KIND_UDP)
        return ERR_BADTYPE;
    if (port == 0) {
        port = port_alloc();
        if (!port)
            return ERR_NOMEM;
        s->port_alloced = true;
    } else {
        if (!port_reserve(port))
            return ERR_DUPLICATE;
    }
    Err rc = UdpBind(port, SocketUdpRx);
    if (rc != ZUZU_OK) {
        s->port_alloced = false;
        port_release(port);
        return rc;
    }
    s->port = port;
    ListAddTail(&s->port_node, &g_by_port[port % SOCK_HASH].node);
    s->shm->local_port = port;
    s->shm->local_ip = netif.ip;
    s->shm->state = SOCK_STATE_BOUND;

    return ZUZU_OK;
}

void SocketClose(Socket *s)
{
    if (s->kind == SOCK_KIND_TCP) {
        if (s->listener) {
            for (int i = 0; i < s->accept_n; i++) {
                tcp_set_ctx(s->accept_q[i], NULL);
                tcp_close(s->accept_q[i]);
            }
            s->accept_n = 0;
        } else {
            SocketTcpTx(s);
        }
        if (s->pcb >= 0) {
            tcp_set_ctx(s->pcb, NULL);
            tcp_close(s->pcb);
            s->pcb = -1;
        }
    }
    if (s->port) {
        ListRemove(&s->port_node);
        UdpUnbind(s->port);
        port_release(s->port);
    }
    ListRemove(&s->id_node);
    ListRemove(&s->sess_node);
    SessionSocketCount(s->session, -1);
    s->shm->state = SOCK_STATE_CLOSED;
    MemUnmap(s->shm);
    HandleClose(s->mem);

    free(s);
}

void SocketCloseAll(int session)
{
    ListHead *sl = SessionSockets(session);
    if (!sl)
        return;
    ListNode *pos, *n;
    LIST_FOR_EACH_SAFE(pos, n, &sl->node) { SocketClose(container_of(pos, Socket, sess_node)); }
}

Socket *SocketByPort(NetPort p)
{
    ListNode *pos;
    LIST_FOR_EACH(pos, &g_by_port[p % SOCK_HASH].node)
    {
        Socket *s = container_of(pos, Socket, port_node);
        if (s->port == p)
            return s;
    }
    return NULL;
}

static void RingRead(SockShm *shm, uint32_t pos, void *dst, uint32_t n)
{
    const uint8_t *ring = SockTxData(shm);
    uint32_t off = pos & (SOCK_RING_SIZE - 1);
    uint32_t first = MIN(n, SOCK_RING_SIZE - off);
    memcpy(dst, ring + off, first);
    memcpy((uint8_t *)dst + first, ring, n - first);
}

static void SocketTcpRx(Socket *s)
{
    if (s->pcb < 0 || s->listener)
        return;
    uint32_t used = s->rx_head - s->shm->rx.tail;
    if (used > SOCK_RING_SIZE)
        used = SOCK_RING_SIZE;
    uint32_t room = SOCK_RING_SIZE - used;
    uint32_t moved = 0;
    bool drained = false;

    uint8_t buf[512];
    while (room) {
        uint16_t want = (uint16_t)MIN(room, sizeof(buf));
        int n = tcp_recv(s->pcb, buf, want);
        if (n <= 0) {
            drained = true;
            break;
        }
        RingWrite(s->shm, s->rx_head, buf, (uint32_t)n);
        s->rx_head += (uint32_t)n;
        room -= (uint32_t)n;
        moved += (uint32_t)n;
    }

    if (moved) {
        ArchSyncBarrier();
        s->shm->rx.head = s->rx_head;
    }
    bool eof = s->peer_closed && drained && !(s->shm->flags & SOCK_F_EOF);
    if (eof)
        s->shm->flags |= SOCK_F_EOF;
    if (moved || eof)
        SessionKick(s->session);
}

static void SocketTcpTx(Socket *s)
{
    if (s->pcb < 0 || s->listener)
        return;
    uint32_t avail = s->shm->tx.head - s->tx_tail;
    if (avail == 0)
        return;
    if (avail > SOCK_RING_SIZE) {
        s->tx_tail = s->shm->tx.head;
        s->shm->tx.tail = s->tx_tail;
        return;
    }
    uint32_t sent = 0;

    uint8_t buf[512];
    while (avail) {
        uint16_t chunk = (uint16_t)MIN(avail, sizeof(buf));
        RingRead(s->shm, s->tx_tail, buf, chunk);
        int n = tcp_send(s->pcb, buf, chunk);
        if (n <= 0)
            break;
        s->tx_tail += (uint32_t)n;
        avail -= (uint32_t)n;
        sent += (uint32_t)n;
        if (n < chunk)
            break;
    }

    if (sent) {
        ArchSyncBarrier();
        s->shm->tx.tail = s->tx_tail;
        SessionKick(s->session);
    }
}

static void SockOnEstablished(int slot)
{
    Socket *s = SockOf(slot);
    if (!s)
        return;
    if (s->listener && slot != s->pcb) {
        if (s->accept_n < SOCK_ACCEPT_MAX) {
            s->accept_q[s->accept_n++] = slot;
            s->shm->accept_pending = (uint32_t)s->accept_n;
            SessionKick(s->session);
        } else {
            tcp_close(slot);
        }
        return;
    }
    s->shm->state = SOCK_STATE_ESTABLISHED;
    SessionKick(s->session);
}

static void SockOnData(int slot)
{
    Socket *s = SockOf(slot);
    if (s && !s->listener)
        SocketTcpRx(s);
}

static void SockOnWritable(int slot)
{
    Socket *s = SockOf(slot);
    if (s && !s->listener)
        SocketTcpTx(s);
}

static void SockOnClose(int slot)
{
    Socket *s = SockOf(slot);
    if (!s || s->listener)
        return;
    s->peer_closed = true;
    SocketTcpRx(s);
}

static void SockOnClosed(int slot, int err)
{
    Socket *s = SockOf(slot);
    if (!s)
        return;
    if (s->listener && slot != s->pcb) {
        for (int i = 0; i < s->accept_n; i++) {
            if (s->accept_q[i] != slot)
                continue;
            memmove(&s->accept_q[i], &s->accept_q[i + 1],
                    (size_t)(s->accept_n - i - 1) * sizeof(s->accept_q[0]));
            s->accept_n--;
            break;
        }
        s->shm->accept_pending = (uint32_t)s->accept_n;
        return;
    }
    s->pcb = -1;
    s->shm->error = err;
    s->shm->state = SOCK_STATE_CLOSED;
    SessionKick(s->session);
}

static const TcpCallbacks g_sock_cb = {
    .on_established = SockOnEstablished,
    .on_data = SockOnData,
    .on_writable = SockOnWritable,
    .on_close = SockOnClose,
    .on_closed = SockOnClosed,
};

static void SocketPumpOne(Socket *s)
{
    if (s->kind == SOCK_KIND_TCP) {
        SocketTcpRx(s);
        SocketTcpTx(s);
        return;
    }
    uint32_t head = s->shm->tx.head;
    uint32_t avail = head - s->tx_tail;
    if (avail == 0)
        return;
    if (avail > SOCK_RING_SIZE) {
        s->tx_tail = head;
        s->shm->tx.tail = head;
        return;
    }

    int budget = 16;
    while (avail >= sizeof(SockDgramHdr) && budget--) {
        SockDgramHdr h;
        RingRead(s->shm, s->tx_tail, &h, sizeof(h));
        uint32_t rec = (sizeof(h) + h.len + 3) & ~3u;
        if (h.len > UDP_MAX_PAYLOAD || rec > avail) {
            s->tx_tail = head;
            break;
        }
        uint8_t buf[UDP_MAX_PAYLOAD];
        RingRead(s->shm, s->tx_tail + sizeof(h), buf, h.len);
        if (s->port) {
            int rc = UdpSend(h.peer_ip, s->port, h.peer_port, buf, h.len);
            if (rc == ERR_BUFFULL)
                break;
        }
        s->tx_tail += rec;
        avail -= rec;
    }
    ArchSyncBarrier();
    s->shm->tx.tail = s->tx_tail;
}

void SocketPump(void)
{
    for (int i = 0; i < SOCK_HASH; i++) {
        ListNode *pos;
        LIST_FOR_EACH(pos, &g_by_id[i].node)
        SocketPumpOne(container_of(pos, Socket, id_node));
    }
}

Err SocketConnect(Socket *s, uint32_t ip, NetPort port)
{
    if (s->kind != SOCK_KIND_TCP)
        return ERR_BADTYPE;
    if (s->pcb >= 0 || s->listener)
        return ERR_BUSY;
    int slot = tcp_connect(ip, port, &g_sock_cb, s);
    if (slot < 0)
        return slot;
    s->pcb = slot;
    s->shm->remote_ip = ip;
    s->shm->remote_port = port;
    s->shm->state = SOCK_STATE_CONNECTING;
    return ZUZU_OK;
}

Err SocketListen(Socket *s, NetPort port, int backlog)
{
    if (s->kind != SOCK_KIND_TCP)
        return ERR_BADTYPE;
    if (s->pcb >= 0 || s->listener)
        return ERR_BUSY;
    if (port == 0)
        return ERR_BADARG;
    if (tcp_pcb_find_listener(netif.ip, port) >= 0)
        return ERR_DUPLICATE;
    int slot = tcp_listen(port, backlog, &g_sock_cb, s);
    if (slot < 0)
        return slot;
    s->pcb = slot;
    s->listener = true;
    s->shm->local_ip = netif.ip;
    s->shm->local_port = port;
    s->shm->state = SOCK_STATE_LISTENING;
    return ZUZU_OK;
}

Err SocketAccept(int session, Socket *listener, Handle mem, uint32_t *id)
{
    if (!listener->listener)
        return ERR_BADARG;
    if (listener->accept_n == 0)
        return ERR_BUFEMPTY;

    Err rc = SocketOpen(session, SOCK_KIND_TCP, mem, id);
    if (rc != ZUZU_OK)
        return rc;
    Socket *ns = SocketFor(*id, session);

    int slot = listener->accept_q[0];
    memmove(&listener->accept_q[0], &listener->accept_q[1],
            (size_t)(listener->accept_n - 1) * sizeof(listener->accept_q[0]));
    listener->accept_n--;
    listener->shm->accept_pending = (uint32_t)listener->accept_n;

    const TcpPcb *pcb = &tcp_pcbs[slot];
    ns->pcb = slot;
    ns->peer_closed = (pcb->state == TCP_CLOSE_WAIT);
    ns->shm->local_ip = pcb->local_ip;
    ns->shm->local_port = pcb->local_port;
    ns->shm->remote_ip = pcb->remote_ip;
    ns->shm->remote_port = pcb->remote_port;
    ns->shm->state = SOCK_STATE_ESTABLISHED;
    tcp_set_ctx(slot, ns);
    SocketTcpRx(ns);
    return ZUZU_OK;
}
