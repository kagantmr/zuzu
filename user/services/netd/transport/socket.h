#ifndef NETD_SOCKET_H
#define NETD_SOCKET_H

#include "../common/globals.h"
#include <net/socket.h>
#include <list.h>
#include <types.h>

#define SOCK_ACCEPT_MAX 16

typedef struct {
    ListNode sess_node;   /* in Session's socket list */
    ListNode id_node;     /* in the id hash */
    ListNode port_node;   /* in the port hash, once bound (UDP) */
    uint32_t id;          /* never reused */
    int session;
    int pcb; /* for TCP, -1 if none */
    bool listener;
    bool peer_closed;
    int accept_n;
    int accept_q[SOCK_ACCEPT_MAX];
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
Err SocketConnect(Socket *s, uint32_t ip, NetPort port);
Err SocketListen(Socket *s, NetPort port, int backlog);
Err SocketAccept(int session, Socket *listener, Handle mem, uint32_t *id);
void SocketClose(Socket *s);
void SocketCloseAll(int session);
Socket *SocketByPort(NetPort p);

#endif /* NETD_SOCKET_H */