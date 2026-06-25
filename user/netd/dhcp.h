#ifndef DHCP_H
#define DHCP_H

#include <zuzu/types.h>
#include "globals.h"

/* BOOTP/DHCP fixed header (RFC 2131). Options follow the magic cookie. */
typedef struct {
    uint8_t  op;          /* 1 = BOOTREQUEST, 2 = BOOTREPLY */
    uint8_t  htype;       /* hardware type, 1 = Ethernet */
    uint8_t  hlen;        /* hardware address length, 6 for MAC */
    uint8_t  hops;        /* relay hop count, 0 from a client */
    uint32_t xid;         /* transaction id, echoed by the server */
    uint16_t secs;        /* seconds since the exchange began */
    uint16_t flags;       /* bit 15 = broadcast (DHCP_FLAG_BROADCAST) */
    uint32_t ciaddr;      /* client IP, only when already bound */
    uint32_t yiaddr;      /* 'your' IP: the address the server is offering */
    uint32_t siaddr;      /* next-server IP (unused here) */
    uint32_t giaddr;      /* relay agent IP (unused here) */
    uint8_t  chaddr[16];  /* client hardware address, our MAC in the first 6 */
    uint8_t  sname[64];   /* server host name (unused) */
    uint8_t  file[128];   /* boot file name (unused) */
    uint32_t cookie;      /* DHCP_MAGIC_COOKIE, options begin right after */
} __attribute__((packed)) dhcp_msg_t;

_Static_assert(sizeof(dhcp_msg_t) == 240, "dhcp_msg_t size");

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_MAGIC_COOKIE 0x63825363u

#define DHCP_HTYPE_ETH 1

/* op */
#define DHCP_OP_REQUEST 1   /* client -> server */
#define DHCP_OP_REPLY   2   /* server -> client */

/* option 53 message types */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_DECLINE  4
#define DHCP_ACK      5
#define DHCP_NAK      6

/* option codes */
#define DHCP_OPT_SUBNET     1
#define DHCP_OPT_ROUTER     3
#define DHCP_OPT_DNS        6
#define DHCP_OPT_REQ_IP     50
#define DHCP_OPT_LEASE      51
#define DHCP_OPT_MSGTYPE    53
#define DHCP_OPT_SERVER_ID  54
#define DHCP_OPT_PARAM_LIST 55
#define DHCP_OPT_END        255
#define DHCP_OPT_PAD        0

#define DHCP_FLAG_BROADCAST 0x8000   /* tell server to broadcast replies */

/* retransmit policy, mirrors DNS's tick-driven backoff */
#define DHCP_TIMEOUT_MS    3000     /* per-attempt timeout before retransmit */
#define DHCP_MAX_RETRIES   4        /* retransmits before restarting the DORA */
#define DHCP_RENEW_RETRY_MS 10000   /* gap between renew/rebind retransmits */
#define DHCP_DEFAULT_LEASE 3600u    /* fallback when the server omits option 51 */
#define DHCP_LEASE_INFINITE 0xFFFFFFFFu

/* RENEWING: T1 reached, unicast REQUEST to the leasing server.
   REBINDING: T2 reached, broadcast REQUEST to any server. */
typedef enum {
    DHCP_INIT, DHCP_SELECTING, DHCP_REQUESTING,
    DHCP_BOUND, DHCP_RENEWING, DHCP_REBINDING
} dhcp_state_t;

/* Fired once, the first time a lease is acquired (renewals don't re-fire). */
typedef void (*dhcp_bound_cb_t)(void);

void dhcp_init(dhcp_bound_cb_t on_bound);
void dhcp_tick(void);
bool dhcp_is_bound(void);

#endif
