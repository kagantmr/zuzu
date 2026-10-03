#ifndef NETD_PROTOCOL_H
#define NETD_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    /* Interface management */
    NETD_GET_NETIF = 1,
    NETD_GET_ALL_IFS,
    NETD_DRVHANDSHAKE, /* Used by lan9118drv to hand its data plane over to netd */
    NETD_DRVNOTIFY,
    NETD_DRVWATCH,

    /* UDP operations */
    SOCKET_UDP,
    SOCKET_UDP_RECV,
    SOCKET_UDP_SENDTO,

    /* TCP operations */
    SOCKET_TCP,
    SOCKET_TCP_RECV,
    SOCKET_TCP_SEND,
    SOCKET_TCP_ACCEPT,

    /* Connection & Lifecycle Management */
    SOCKET_BIND,
    SOCKET_CONNECT,
    SOCKET_LISTEN,
    SOCKET_CLOSE,

    /* lower-level Sockets */
    SOCKET_IP,
    SOCKET_FRAME,
    
    NETD_OPCODE_COUNT
} NetdOpcode;


// zuzu error types...

#ifdef __cplusplus
}
#endif

#endif // NETD_PROTOCOL_H