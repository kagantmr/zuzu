#include "socket.h"
#include "user/services/netd/common/globals.h"
#include <zuzu/zuzu.h>
#include <list.h>
#include <stdlib.h>

#define CONNTABLE_BUCKETS 128

static ListHead conntable[CONNTABLE_BUCKETS];

static inline uint32_t ConnTableHash(NetPort port)
{
    return ((uint32_t)port * 2654435761U) >> 25; // Knuth's multiplicative hash, top bits
}

void ConnTableInit(void)
{
    for (int i = 0; i < CONNTABLE_BUCKETS; i++)
        ListInit(&conntable[i]);
}

ConnTableEnt *ConnTableLookup(NetPort port)
{
    ListHead *head = &conntable[ConnTableHash(port)];
    ListNode *pos;
    LIST_FOR_EACH(pos, &head->node)
    {
        ConnTableEnt *ent = container_of(pos, ConnTableEnt, bucket_link);
        if (port == ent->port) {
            return ent;
        }
    }
    return NULL;
}

bool ConnTableInsert(NetPort port, void *tx_rbuf, void *rx_rbuf, Handle tx_shm, Handle rx_shm,
                     Handle tx_ntfn, Handle rx_ntfn, Handle ctl)
{
    if (ConnTableLookup(port))
        return false;

    ConnTableEnt *ent = calloc(1, sizeof(ConnTableEnt));
    ListHead *head = &conntable[ConnTableHash(port)];

    ent->port = port;
    ent->ctlport = ctl;
    ent->tx_ring = tx_rbuf;
    ent->rx_ring = rx_rbuf;
    ent->tx_shm = tx_shm;
    ent->rx_shm = rx_shm;
    ent->tx_ntf = tx_ntfn;
    ent->rx_ntf = rx_ntfn;

    ListAddTail(&ent->bucket_link, &head->node);

    return true;
}

bool ConnTableRemove(NetPort port)
{
    ConnTableEnt *ent = ConnTableLookup(port);
    if (!ent)
        return false;
    ListRemove(&ent->bucket_link);
    free(ent);
    return true;
}

#define UDP_SOCK_RBUFSZ (8 * 1024) // 8KB

ConnTableEnt *ConnTableCreateEntry(void)
{
    ConnTableEnt *conn = calloc(1, sizeof(ConnTableEnt));
    if (!conn)
        return NULL;

    conn->tx_shm = CreateMem(UDP_SOCK_RBUFSZ / 4096);
    if (conn->tx_shm < 0)
        goto fail_conn;

    conn->rx_shm = CreateMem(UDP_SOCK_RBUFSZ / 4096);
    if (conn->rx_shm < 0)
        goto fail_tx_shm;

    conn->rx_ring = MemMap(conn->rx_shm, 0, PROT_READ);
    if (!conn->rx_ring)
        goto fail_rx_shm;

    conn->tx_ring = MemMap(conn->tx_shm, 0, PROT_RW);
    if (!conn->tx_ring)
        goto fail_rx_ring;

    conn->tx_ntf = CreateEvent();
    if (conn->tx_ntf < 0)
        goto fail_tx_ring;

    conn->rx_ntf = CreateEvent();
    if (conn->rx_ntf < 0)
        goto fail_tx_ntfn;

    // set ctl port
    conn->ctlport = CreatePort();
    if (conn->ctlport < 0)
        goto fail_rx_ntfn;

    return conn;

fail_rx_ntfn:
    HandleClose(conn->rx_ntf);
fail_tx_ntfn:
    HandleClose(conn->tx_ntf);
fail_tx_ring:
    MemUnmap(conn->tx_ring);
fail_rx_ring:
    MemUnmap(conn->rx_ring);
fail_rx_shm:
    HandleClose(conn->rx_shm);
fail_tx_shm:
    HandleClose(conn->tx_shm);
fail_conn:
    free(conn);
    return NULL;
}