#include "socket.h"
#include "user/services/netd/common/globals.h"
#include "zuzu/cap.h"
#include "zuzu/memprot.h"
#include "zuzu/ntfn.h"
#include "zuzu/types.h"
#include <zuzu/umem.h>
#include <zuzu/msg.h>
#include <stdlib.h>

#define CONNTABLE_BUCKETS 128

static ListHead conntable[CONNTABLE_BUCKETS];

static inline uint32_t conntable_hash(port_t port)
{
    return ((uint32_t)port * 2654435761u) >> 25; // Knuth's multiplicative hash, top bits
}

void ConnTableInit(void)
{
    for (int i = 0; i < CONNTABLE_BUCKETS; i++)
        list_init(&conntable[i]);
}

ConnTableEnt *ConnTableLookup(port_t port)
{
    ListHead *head = &conntable[conntable_hash(port)];
    ListNode *pos;
    list_for_each(pos, &head->node)
    {
        ConnTableEnt *ent = container_of(pos, ConnTableEnt, bucket_link);
        if (port == ent->port) {
            return ent;
        }
    }
    return NULL;
}

bool ConnTableInsert(port_t port, void *tx_rbuf, void *rx_rbuf, Handle tx_shm, Handle rx_shm,
                     Handle tx_ntfn, Handle rx_ntfn, Handle ctl)
{
    if (ConnTableLookup(port))
        return false;

    ConnTableEnt *ent = calloc(1, sizeof(ConnTableEnt));
    ListHead *head = &conntable[conntable_hash(port)];

    ent->port = port;
    ent->ctlport = ctl;
    ent->tx_ring = tx_rbuf;
    ent->rx_ring = rx_rbuf;
    ent->tx_shm = tx_shm;
    ent->rx_shm = rx_shm;
    ent->tx_ntf = tx_ntfn;
    ent->rx_ntf = rx_ntfn;

    list_add_tail(&ent->bucket_link, &head->node);

    return true;
}

bool ConnTableRemove(port_t port)
{
    ConnTableEnt *ent = ConnTableLookup(port);
    if (!ent)
        return false;
    list_remove(&ent->bucket_link);
    free(ent);
    return true;
}

#define UDP_SOCK_RBUFSZ (8 * 1024) // 8KB

ConnTableEnt *ConnTableCreateEntry(void)
{
    ConnTableEnt *conn = calloc(1, sizeof(ConnTableEnt));
    if (!conn)
        return NULL;

    conn->tx_shm = ZuzuShmemCreate(UDP_SOCK_RBUFSZ);
    if (conn->tx_shm < 0)
        goto fail_conn;

    conn->rx_shm = ZuzuShmemCreate(UDP_SOCK_RBUFSZ);
    if (conn->rx_shm < 0)
        goto fail_tx_shm;

    conn->rx_ring = ZuzuMemMap(conn->rx_shm, 0, PROT_RW, 0);
    if (!conn->rx_ring)
        goto fail_rx_shm;

    conn->tx_ring = ZuzuMemMap(conn->tx_shm, 0, PROT_RW, 0);
    if (!conn->tx_ring)
        goto fail_rx_ring;

    conn->tx_ntf = ZuzuNtfnCreate();
    if (conn->tx_ntf < 0)
        goto fail_tx_ring;

    conn->rx_ntf = ZuzuNtfnCreate();
    if (conn->rx_ntf < 0)
        goto fail_tx_ntfn;

    // set ctl port
    conn->ctlport = ZuzuPortCreate();
    if (conn->ctlport < 0)
        goto fail_rx_ntfn;

        
    return conn;

fail_rx_ntfn:
    ZuzuDestroy(conn->rx_ntf);
fail_tx_ntfn:
    ZuzuDestroy(conn->tx_ntf);
fail_tx_ring:
    ZuzuMemUnmap(conn->tx_ring);
fail_rx_ring:
    ZuzuMemUnmap(conn->rx_ring);
fail_rx_shm:
    ZuzuDestroy(conn->rx_shm);
fail_tx_shm:
    ZuzuDestroy(conn->tx_shm);
fail_conn:
    free(conn);
    return NULL;
}