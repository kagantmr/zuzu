#include "dhcp.h"
#include "globals.h"
#include "udp.h"
#include <stddef.h>
#include <convert.h>
#include <zuzu/log.h>

static struct {
    dhcp_state_t state;
    uint32_t     xid;            /* our transaction id, 32-bit */
    uint32_t     sent_ms;        /* for retransmit timing */
    uint8_t      retries;
    ipv4_addr_t  offered_ip;     /* yiaddr from the OFFER, echoed in REQUEST */
    ipv4_addr_t  server_id;      /* option 54, which server we accepted */
    uint32_t     start_ms;       /* when the current DORA exchange began (secs field) */
    uint32_t     bound_ms;       /* when the active lease was last (re)acquired */
    uint32_t     lease_secs;     /* lease duration from option 51 */
    uint32_t     t1_secs;        /* renew threshold (lease/2) */
    uint32_t     t2_secs;        /* rebind threshold (lease*7/8) */
    dhcp_bound_cb_t on_bound;    /* first-bind notification, may be NULL */
    bool         notified;       /* on_bound already fired? */
} dhcp;

static size_t dhcp_opt_u8(uint8_t *p, uint8_t code, uint8_t val)
{
    p[0] = code;
    p[1] = 1;
    p[2] = val;
    return 3;
}

static size_t dhcp_opt_u32(uint8_t *p, uint8_t code, uint32_t val)
{
    p[0] = code;
    p[1] = 4;
    memcpy(&p[2], &val, 4);
    return 6;
}

static size_t dhcp_opt_raw(uint8_t *p, uint8_t code, const uint8_t *v, uint8_t len)
{
    p[0] = code;
    p[1] = len;
    memcpy(&p[2], v, len);
    return 2 + (size_t)len;
}

static size_t dhcp_build(uint8_t *pkt)
{
    dhcp_msg_t *m = (dhcp_msg_t *)pkt;
    memset(m, 0, sizeof(dhcp_msg_t));
    m->op = DHCP_OP_REQUEST;
    m->htype = DHCP_HTYPE_ETH;
    m->hlen = 6;
    m->xid = dhcp.xid;
    memcpy(m->chaddr, netif.mac, 6);
    m->flags = htons(DHCP_FLAG_BROADCAST);
    m->cookie = htonl(DHCP_MAGIC_COOKIE);
    uint32_t elapsed = (net_now_ms() - dhcp.start_ms) / 1000;
    if (elapsed > 0xFFFF) elapsed = 0xFFFF;
    m->secs = htons((uint16_t)elapsed);
    return sizeof(dhcp_msg_t);
}

static int dhcp_send_discover(void)
{
    uint8_t buf[sizeof(dhcp_msg_t) + 64];

    size_t len = dhcp_build(buf);

    len += dhcp_opt_u8(buf + len, DHCP_OPT_MSGTYPE, DHCP_DISCOVER);
    static const uint8_t params[] = { DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS };
    len += dhcp_opt_raw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = udp_tx(BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK) {
        dhcp.state = DHCP_SELECTING;
        dhcp.sent_ms = net_now_ms();
    }
    return rc;
}

static int dhcp_send_request(void)
{
    uint8_t buf[sizeof(dhcp_msg_t) + 64];

    size_t len = dhcp_build(buf);

    len += dhcp_opt_u8(buf + len, DHCP_OPT_MSGTYPE, DHCP_REQUEST);
    len += dhcp_opt_u32(buf + len, DHCP_OPT_REQ_IP, dhcp.offered_ip);
    len += dhcp_opt_u32(buf + len, DHCP_OPT_SERVER_ID, dhcp.server_id);

    static const uint8_t params[] = { DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS };
    len += dhcp_opt_raw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = udp_tx(BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK) {
        dhcp.state = DHCP_REQUESTING;
        dhcp.sent_ms = net_now_ms();
    }
    return rc;
}

/* REQUEST for an existing lease (RENEWING/REBINDING). Unlike the selecting
   REQUEST, ciaddr carries our current address and we omit options 50/54
   (RFC 2131 4.3.2). RENEWING unicasts to the server; REBINDING broadcasts. */
static int dhcp_send_renew(ipv4_addr_t dst)
{
    uint8_t buf[sizeof(dhcp_msg_t) + 64];

    size_t len = dhcp_build(buf);
    dhcp_msg_t *m = (dhcp_msg_t *)buf;
    m->ciaddr = netif.ip;        /* raw; proves which lease we're renewing */
    m->flags  = 0;               /* we hold an IP, so accept a unicast reply */

    len += dhcp_opt_u8(buf + len, DHCP_OPT_MSGTYPE, DHCP_REQUEST);

    static const uint8_t params[] = { DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS };
    len += dhcp_opt_raw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = udp_tx(dst, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK)
        dhcp.sent_ms = net_now_ms();
    return rc;
}

static const uint8_t *dhcp_find_option(const uint8_t *data, uint16_t len,
                                       uint8_t code, uint8_t *out_len)
{
    const uint8_t *cur = data + sizeof(dhcp_msg_t);
    const uint8_t *end = data + len;

    while (cur < end) {
        if (*cur == DHCP_OPT_END) return NULL;
        if (*cur == DHCP_OPT_PAD) { cur++; continue; }

        if (cur + 1 >= end) return NULL;
        uint8_t optlen = cur[1];
        if (cur + 2 + optlen > end) return NULL;

        if (*cur == code) { *out_len = optlen; return cur + 2; }
        cur += 2 + optlen;
    }
    return NULL;
}

static void dhcp_restart(void)
{
    dhcp.state    = DHCP_INIT;
    dhcp.xid      = net_now_ms();
    dhcp.start_ms = net_now_ms();
    dhcp.retries  = 0;
    dhcp_send_discover();
}

/* Apply an ACK's parameters and (re)enter BOUND. Shared by the initial
   acquire and by renew/rebind, so the lease clock restarts every ACK. */
static void dhcp_enter_bound(const uint8_t *data, uint16_t len)
{
    const dhcp_msg_t *m = (const dhcp_msg_t *)data;
    uint8_t optlen;
    const uint8_t *p;

    netif.ip = m->yiaddr;                        /* raw */

    p = dhcp_find_option(data, len, DHCP_OPT_SUBNET, &optlen);
    if (p && optlen == 4) memcpy(&netif.netmask, p, 4);   /* raw */

    p = dhcp_find_option(data, len, DHCP_OPT_ROUTER, &optlen);
    if (p && optlen >= 4) memcpy(&netif.gateway, p, 4);   /* raw, first of the list */

    p = dhcp_find_option(data, len, DHCP_OPT_DNS, &optlen);
    if (p && optlen >= 4) memcpy(&netif.dns, p, 4);       /* raw, first of the list */

    dhcp.lease_secs = DHCP_DEFAULT_LEASE;
    p = dhcp_find_option(data, len, DHCP_OPT_LEASE, &optlen);
    if (p && optlen == 4) {
        uint32_t lease_net;
        memcpy(&lease_net, p, 4);
        dhcp.lease_secs = ntohl(lease_net);               /* the one swap */
    }
    /* T1/T2 per RFC 2131: renew at half the lease, rebind at 7/8. */
    dhcp.t1_secs = dhcp.lease_secs / 2;
    dhcp.t2_secs = dhcp.lease_secs - dhcp.lease_secs / 8;

    dhcp.bound_ms = net_now_ms();
    dhcp.retries  = 0;
    dhcp.state    = DHCP_BOUND;
    LOG_INFO(LOG_TAG, "bound %u.%u.%u.%u lease %us", IP4(netif.ip), dhcp.lease_secs);

    if (!dhcp.notified) {
        dhcp.notified = true;
        if (dhcp.on_bound) dhcp.on_bound();
    }
}

static void dhcp_recv(ipv4_addr_t src_ip, uint16_t src_port,
                      uint16_t dst_port, const uint8_t *data, uint16_t len)
{
    if (len < sizeof(dhcp_msg_t)) return;
    const dhcp_msg_t *m = (const dhcp_msg_t *)data;
    if (m->op != DHCP_OP_REPLY) return;
    if (ntohl(m->cookie) != DHCP_MAGIC_COOKIE) return;
    if (m->xid != dhcp.xid) return;

    uint8_t optlen;
    const uint8_t *p = dhcp_find_option(data, len, DHCP_OPT_MSGTYPE, &optlen);
    if (!p || optlen != 1) return;
    uint8_t msgtype = *p;

    bool renewing = (dhcp.state == DHCP_RENEWING || dhcp.state == DHCP_REBINDING);

    if (dhcp.state == DHCP_SELECTING && msgtype == DHCP_OFFER) {
        dhcp.offered_ip = m->yiaddr;                 /* raw, no swap */

        p = dhcp_find_option(data, len, DHCP_OPT_SERVER_ID, &optlen);
        if (!p || optlen != 4) return;               /* OFFER without server id is unusable */
        memcpy(&dhcp.server_id, p, 4);               /* raw */

        dhcp_send_request();                         /* it sets state = REQUESTING */

    } else if ((dhcp.state == DHCP_REQUESTING || renewing) && msgtype == DHCP_ACK) {
        /* refresh which server holds the lease (relevant after a rebind) */
        p = dhcp_find_option(data, len, DHCP_OPT_SERVER_ID, &optlen);
        if (p && optlen == 4) memcpy(&dhcp.server_id, p, 4);
        dhcp_enter_bound(data, len);

    } else if ((dhcp.state == DHCP_REQUESTING || renewing) && msgtype == DHCP_NAK) {
        dhcp_restart();                              /* lease refused: start over */
    }
}

void dhcp_init(dhcp_bound_cb_t on_bound)
{
    memset(&dhcp, 0, sizeof(dhcp));
    dhcp.on_bound = on_bound;
    netif.ip = netif.netmask = netif.gateway = netif.dns = 0;
    udp_bind(DHCP_CLIENT_PORT, dhcp_recv);
    dhcp_restart();
}

bool dhcp_is_bound(void)
{
    return dhcp.state == DHCP_BOUND ||
           dhcp.state == DHCP_RENEWING ||
           dhcp.state == DHCP_REBINDING;   /* lease still valid while renewing */
}

void dhcp_tick(void)
{
    uint32_t now = net_now_ms();

    switch (dhcp.state) {
    case DHCP_INIT:
        return;                                  /* nothing in flight */

    case DHCP_SELECTING:
    case DHCP_REQUESTING:
        if ((int32_t)(now - dhcp.sent_ms) < DHCP_TIMEOUT_MS)
            return;                              /* current attempt still has time */
        if (dhcp.retries >= DHCP_MAX_RETRIES) {
            dhcp_restart();                      /* gave up: back to square one */
            return;
        }
        dhcp.retries++;
        if (dhcp.state == DHCP_SELECTING) dhcp_send_discover();
        else                              dhcp_send_request();
        return;

    case DHCP_BOUND: {
        if (dhcp.lease_secs == DHCP_LEASE_INFINITE)
            return;                              /* permanent lease, never renew */
        uint32_t held = (now - dhcp.bound_ms) / 1000;
        if (held >= dhcp.t2_secs) {              /* T1 missed, broadcast to anyone */
            dhcp.state = DHCP_REBINDING;
            dhcp_send_renew(BROADCAST_IP);
        } else if (held >= dhcp.t1_secs) {       /* T1: unicast to our server */
            dhcp.state = DHCP_RENEWING;
            dhcp_send_renew(dhcp.server_id);
        }
        return;
    }

    case DHCP_RENEWING:
    case DHCP_REBINDING: {
        uint32_t held = (now - dhcp.bound_ms) / 1000;
        if (held >= dhcp.lease_secs) {           /* lease fully expired: drop it */
            netif.ip = netif.netmask = netif.gateway = netif.dns = 0;
            dhcp_restart();
            return;
        }
        if (dhcp.state == DHCP_RENEWING && held >= dhcp.t2_secs)
            dhcp.state = DHCP_REBINDING;         /* escalate from unicast to broadcast */
        if ((int32_t)(now - dhcp.sent_ms) >= DHCP_RENEW_RETRY_MS)
            dhcp_send_renew(dhcp.state == DHCP_RENEWING ? dhcp.server_id : BROADCAST_IP);
        return;
    }
    }
}
