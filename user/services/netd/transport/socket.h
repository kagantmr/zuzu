#ifndef NETD_SOCKET_H
#define NETD_SOCKET_H

#include <zuzu/types.h>
#include <list.h>
#include "../common/globals.h"

typedef Handle UdpSocket;
typedef Handle TcpSocket;

typedef struct {
   port_t port;
   void *tx_ring;
   void *rx_ring;
   Handle tx_shm;
   Handle rx_shm;
   Handle tx_ntf;
   Handle rx_ntf;
   Handle ctlport;
   ListNode bucket_link;
} ConnTableEnt;

bool ConnTableRemove(port_t port);
bool ConnTableInsert(port_t port, void* tx_rbuf, void* rx_rbuf, Handle tx_shm, Handle rx_shm,
                      Handle tx_ntfn, Handle rx_ntfn, Handle ctl);
ConnTableEnt *ConnTableLookup(port_t port);
void ConnTableInit(void);

UdpSocket *CreateUdpSocket(void);

#endif /* NETD_SOCKET_H */