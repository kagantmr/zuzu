#ifndef NETD_SOCKET_H
#define NETD_SOCKET_H

#include "../common/globals.h"
#include <net/protocols/socket.h>
#include <list.h>
#include <types.h>

typedef struct {
    ListNode sess_node;   /* in Session's socket list */
    ListNode id_node;     /* in the id hash */
    ListNode port_node;   /* in the port hash, once bound (UDP) */
    uint32_t id;          /* never reused */
    int session;
    uint32_t kind;
    Handle mem;
    SockShm *shm;
    NetPort port;
    bool port_alloced;
    uint32_t rx_head, tx_tail;
} Socket;

void SocketInit(void);
void SocketPump(void);
Err SocketOpen(int session, uint32_t kind, Handle mem, uint32_t *id);
Socket *SocketFor(uint32_t id, int session);
Err SocketBind(Socket *s, NetPort port);
void SocketClose(Socket *s);
void SocketCloseAll(int session);
Socket *SocketByPort(NetPort p);

#endif /* NETD_SOCKET_H */