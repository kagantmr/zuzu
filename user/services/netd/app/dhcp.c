#include "dhcp.h"
#include "../common/globals.h"
#include "../link/arp.h"
#include "../transport/port.h"
#include "../transport/udp.h"
#include <convert.h>
#include <stddef.h>
#include <util/log.h>
#include "../common/netif.h"

#define DHCP_ACK_BUFSZ 576

typedef struct {
    NetInterface *nif; /* interface this client configures */
    bool enabled;      /* DhcpStart() was called for nif */
    DhcpState state;
    uint32_t xid;     /* our transaction id, 32-bit */
    uint32_t sent_ms; /* for retransmit timing */
    uint8_t retries;
    Ipv4Addr offered_ip;             /* yiaddr from the OFFER, echoed in REQUEST */
    Ipv4Addr server_id;              /* option 54, which server we accepted */
    uint32_t start_ms;               /* when the current DORA exchange began (secs field) */
    uint32_t bound_ms;               /* when the active lease was last (re)acquired */
    uint32_t lease_secs;             /* lease duration from option 51 */
    uint32_t t1_secs;                /* renew threshold (lease/2) */
    uint32_t t2_secs;                /* rebind threshold (lease*7/8) */
    bool notified;                   /* on_bound already fired? */
    uint8_t ack_buf[DHCP_ACK_BUFSZ]; /* ACK held while ACD probes offered_ip */
    uint16_t ack_len;                /* bytes valid in ack_buf */
    bool link_down;
} DhcpClient;

static DhcpClient clients[NETIF_MAX];
static DhcpBoundCb on_bound_cb;

static size_t DhcpOptU8(uint8_t *p, uint8_t code, uint8_t val)
{
    p[0] = code;
    p[1] = 1;
    p[2] = val;
    return 3;
}

static size_t DhcpOptU32(uint8_t *p, uint8_t code, uint32_t val)
{
    p[0] = code;
    p[1] = 4;
    memcpy(&p[2], &val, 4);
    return 6;
}

static size_t DhcpOptRaw(uint8_t *p, uint8_t code, const uint8_t *v, uint8_t len)
{
    p[0] = code;
    p[1] = len;
    memcpy(&p[2], v, len);
    return 2 + (size_t)len;
}

static size_t DhcpBuild(DhcpClient *c, uint8_t *pkt)
{
    DhcpMsg *m = (DhcpMsg *)pkt;
    memset(m, 0, sizeof(DhcpMsg));
    m->op = DHCP_OP_REQUEST;
    m->htype = DHCP_HTYPE_ETH;
    m->hlen = 6;
    m->xid = c->xid;
    memcpy(m->chaddr, c->nif->mac, 6);
    m->flags = htons(DHCP_FLAG_BROADCAST);
    m->cookie = htonl(DHCP_MAGIC_COOKIE);
    uint32_t elapsed = (NetNowMs() - c->start_ms) / 1000;
    if (elapsed > 0xFFFF)
        elapsed = 0xFFFF;
    m->secs = htons((uint16_t)elapsed);
    return sizeof(DhcpMsg);
}

static __attribute__((cold)) int DhcpSendDiscover(DhcpClient *c)
{
    uint8_t buf[sizeof(DhcpMsg) + 64];

    size_t len = DhcpBuild(c, buf);

    len += DhcpOptU8(buf + len, DHCP_OPT_MSGTYPE, DHCP_DISCOVER);
    static const uint8_t params[] = {DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    len += DhcpOptRaw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = UdpSendVia(c->nif, BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK) {
        c->state = DHCP_SELECTING;
        c->sent_ms = NetNowMs();
    }
    return rc;
}

static __attribute__((cold)) int DhcpSendRequest(DhcpClient *c)
{
    uint8_t buf[sizeof(DhcpMsg) + 64];

    size_t len = DhcpBuild(c, buf);

    len += DhcpOptU8(buf + len, DHCP_OPT_MSGTYPE, DHCP_REQUEST);
    len += DhcpOptU32(buf + len, DHCP_OPT_REQ_IP, c->offered_ip);
    len += DhcpOptU32(buf + len, DHCP_OPT_SERVER_ID, c->server_id);

    static const uint8_t params[] = {DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    len += DhcpOptRaw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = UdpSendVia(c->nif, BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK) {
        c->state = DHCP_REQUESTING;
        c->sent_ms = NetNowMs();
    }
    return rc;
}

/* REQUEST for an existing lease (RENEWING/REBINDING). Unlike the selecting
   REQUEST, ciaddr carries our current address and we omit options 50/54
   (RFC 2131 4.3.2). RENEWING unicasts to the server; REBINDING broadcasts. */
static __attribute__((cold)) int DhcpSendRenew(DhcpClient *c, Ipv4Addr dst)
{
    uint8_t buf[sizeof(DhcpMsg) + 64];

    size_t len = DhcpBuild(c, buf);
    DhcpMsg *m = (DhcpMsg *)buf;
    m->ciaddr = c->nif->ip; /* raw; proves which lease we're renewing */
    m->flags = 0;         /* we hold an IP, so accept a unicast reply */

    len += DhcpOptU8(buf + len, DHCP_OPT_MSGTYPE, DHCP_REQUEST);

    static const uint8_t params[] = {DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    len += DhcpOptRaw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = UdpSendVia(c->nif, dst, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK)
        c->sent_ms = NetNowMs();
    return rc;
}

static __attribute__((cold)) int DhcpSendReboot(DhcpClient *c)
{
    uint8_t buf[sizeof(DhcpMsg) + 64];

    size_t len = DhcpBuild(c, buf);

    len += DhcpOptU8(buf + len, DHCP_OPT_MSGTYPE, DHCP_REQUEST);
    len += DhcpOptU32(buf + len, DHCP_OPT_REQ_IP, c->nif->ip);

    static const uint8_t params[] = {DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    len += DhcpOptRaw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = UdpSendVia(c->nif, BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK)
        c->sent_ms = NetNowMs();
    return rc;
}

static bool DhcpLeaseValid(const DhcpClient *c)
{
    if (c->lease_secs == DHCP_LEASE_INFINITE)
        return true;
    return (NetNowMs() - c->bound_ms) / 1000 < c->lease_secs;
}

/* DHCPDECLINE (RFC 2131 4.4.4): tell the server the offered address is already
   in use so it won't hand it out again, then restart the exchange. Broadcast,
   ciaddr 0, carrying the declined address (option 50) and the server id. */
static __attribute__((cold)) int DhcpSendDecline(DhcpClient *c)
{
    uint8_t buf[sizeof(DhcpMsg) + 64];

    size_t len = DhcpBuild(c, buf);

    len += DhcpOptU8(buf + len, DHCP_OPT_MSGTYPE, DHCP_DECLINE);
    len += DhcpOptU32(buf + len, DHCP_OPT_REQ_IP, c->offered_ip);
    len += DhcpOptU32(buf + len, DHCP_OPT_SERVER_ID, c->server_id);

    buf[len++] = DHCP_OPT_END;

    return UdpSendVia(c->nif, BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
}

static const uint8_t *DhcpFindOption(const uint8_t *data, uint16_t len, uint8_t code,
                                     uint8_t *out_len)
{
    const uint8_t *cur = data + sizeof(DhcpMsg);
    const uint8_t *end = data + len;

    while (cur < end) {
        if (*cur == DHCP_OPT_END)
            return NULL;
        if (*cur == DHCP_OPT_PAD) {
            cur++;
            continue;
        }

        if (cur + 1 >= end)
            return NULL;
        uint8_t optlen = cur[1];
        if (cur + 2 + optlen > end)
            return NULL;

        if (*cur == code) {
            *out_len = optlen;
            return cur + 2;
        }
        cur += 2 + optlen;
    }
    return NULL;
}

static __attribute__((cold)) void DhcpRestart(DhcpClient *c)
{
    c->state = DHCP_INIT;
    c->xid = NetNowMs();
    c->start_ms = NetNowMs();
    c->retries = 0;
    DhcpSendDiscover(c);
}

/* Apply an ACK's parameters and (re)enter BOUND. Shared by the initial
   acquire and by renew/rebind, so the lease clock restarts every ACK. */
static __attribute__((cold)) void DhcpEnterBound(DhcpClient *c, const uint8_t *data, uint16_t len)
{
    const DhcpMsg *m = (const DhcpMsg *)data;
    uint8_t optlen;
    const uint8_t *p;

    c->nif->ip = m->yiaddr; /* raw */

    p = DhcpFindOption(data, len, DHCP_OPT_SUBNET, &optlen);
    if (p && optlen == 4)
        memcpy(&c->nif->netmask, p, 4); /* raw */

    p = DhcpFindOption(data, len, DHCP_OPT_ROUTER, &optlen);
    if (p && optlen >= 4)
        memcpy(&c->nif->gateway, p, 4); /* raw, first of the list */

    p = DhcpFindOption(data, len, DHCP_OPT_DNS, &optlen);
    if (p && optlen >= 4)
        memcpy(&c->nif->dns, p, 4); /* raw, first of the list */

    c->lease_secs = DHCP_DEFAULT_LEASE;
    p = DhcpFindOption(data, len, DHCP_OPT_LEASE, &optlen);
    if (p && optlen == 4) {
        uint32_t lease_net;
        memcpy(&lease_net, p, 4);
        c->lease_secs = ntohl(lease_net); /* the one swap */
    }
    /* T1/T2 per RFC 2131: renew at half the lease, rebind at 7/8. */
    c->t1_secs = c->lease_secs / 2;
    c->t2_secs = c->lease_secs - (c->lease_secs / 8);

    c->bound_ms = NetNowMs();
    c->retries = 0;
    c->state = DHCP_BOUND;
    LOG_INFO(LOG_TAG, "bound %u.%u.%u.%u lease %us", IP4(c->nif->ip), c->lease_secs);

    if (!c->notified) {
        c->notified = true;
        if (on_bound_cb)
            on_bound_cb(c->nif);
    }
}

/* ARP conflict-detection verdict for the offered IP. On a conflict, decline the
   lease and restart DORA so the server picks a different address; otherwise the
   stashed ACK is now safe to commit. */
static __attribute__((cold)) void DhcpAcdDone(NetInterface *nif, bool conflict)
{
    DhcpClient *c = &clients[nif->index];
    if (conflict) {
        LOG_WARN(LOG_TAG, "%u.%u.%u.%u in use, declining lease", IP4(c->offered_ip));
        DhcpSendDecline(c);
        DhcpRestart(c);
        return;
    }
    DhcpEnterBound(c, c->ack_buf, c->ack_len);
}

/* Stash the ACK (the UDP buffer won't outlive this call) and probe the offered
   address before committing it. DhcpAcdDone resumes once ACD reports back. */
static __attribute__((cold)) void DhcpBeginAcd(DhcpClient *c, const uint8_t *data, uint16_t len)
{
    if (len > DHCP_ACK_BUFSZ)
        len = DHCP_ACK_BUFSZ;
    memcpy(c->ack_buf, data, len);
    c->ack_len = len;
    c->state = DHCP_PROBING;
    LOG_DEBUG(LOG_TAG, "probing %u.%u.%u.%u before binding", IP4(c->offered_ip));
    ArpAcdStart(c->nif, c->offered_ip, DhcpAcdDone);
}

static __attribute__((cold)) void DhcpRecv(Ipv4Addr src_ip, NetPort src_port, NetPort dst_port,
                                           const uint8_t *data, uint16_t len)
{
    (void)src_ip;
    (void)src_port;
    (void)dst_port;
    if (len < sizeof(DhcpMsg))
        return;
    const DhcpMsg *m = (const DhcpMsg *)data;
    if (m->op != DHCP_OP_REPLY)
        return;
    if (ntohl(m->cookie) != DHCP_MAGIC_COOKIE)
        return;
    DhcpClient *c = NULL;
    for (int i = 0; i < NETIF_MAX; i++) {
        if (clients[i].enabled && clients[i].xid == m->xid &&
            memcmp(m->chaddr, clients[i].nif->mac, 6) == 0) {
            c = &clients[i];
            break;
        }
    }
    if (!c)
        return;

    uint8_t optlen;
    const uint8_t *p = DhcpFindOption(data, len, DHCP_OPT_MSGTYPE, &optlen);
    if (!p || optlen != 1)
        return;
    uint8_t msgtype = *p;

    bool renewing = (c->state == DHCP_RENEWING || c->state == DHCP_REBINDING ||
                     c->state == DHCP_REBOOTING);

    if (c->state == DHCP_SELECTING && msgtype == DHCP_OFFER) {
        c->offered_ip = m->yiaddr; /* raw, no swap */

        p = DhcpFindOption(data, len, DHCP_OPT_SERVER_ID, &optlen);
        if (!p || optlen != 4)
            return;                    /* OFFER without server id is unusable */
        memcpy(&c->server_id, p, 4); /* raw */

        DhcpSendRequest(c); /* it sets state = REQUESTING */

    } else if ((c->state == DHCP_REQUESTING || renewing) && msgtype == DHCP_ACK) {
        /* refresh which server holds the lease (relevant after a rebind) */
        p = DhcpFindOption(data, len, DHCP_OPT_SERVER_ID, &optlen);
        if (p && optlen == 4)
            memcpy(&c->server_id, p, 4);
        /* Initial assignment: ARP-probe the offered IP before committing it.
           Renew/rebind already own the address, so bind straight away. */
        if (renewing)
            DhcpEnterBound(c, data, len);
        else
            DhcpBeginAcd(c, data, len);

    } else if ((c->state == DHCP_REQUESTING || renewing) && msgtype == DHCP_NAK) {
        if (renewing)
            LOG_INFO(LOG_TAG, "lease on %u.%u.%u.%u refused, starting over", IP4(c->nif->ip));
        c->nif->ip = c->nif->netmask = c->nif->gateway = c->nif->dns = 0;
        DhcpRestart(c);
    }
}

__attribute__((cold)) void DhcpInit(DhcpBoundCb on_bound)
{
    memset(clients, 0, sizeof(clients));
    on_bound_cb = on_bound;
    /* DHCP's client port is fixed at 68 (servers reply there), so claim it
       explicitly rather than allocating an ephemeral one. */
    PortReserve(DHCP_CLIENT_PORT);
    UdpBind(DHCP_CLIENT_PORT, DhcpRecv);
}

__attribute__((cold)) void DhcpStart(NetInterface *nif)
{
    DhcpClient *c = &clients[nif->index];
    memset(c, 0, sizeof(*c));
    c->nif = nif;
    c->enabled = true;
    nif->ip = nif->netmask = nif->gateway = nif->dns = 0;
    DhcpRestart(c);
}

bool DhcpIsBound(const NetInterface *nif)
{
    const DhcpClient *c = &clients[nif->index];
    return c->enabled && (c->state == DHCP_BOUND || c->state == DHCP_RENEWING ||
                          c->state == DHCP_REBINDING ||
                          c->state == DHCP_REBOOTING); /* lease still valid while renewing */
}

void DhcpLinkDown(NetInterface *nif) { clients[nif->index].link_down = true; }

void DhcpLinkUp(NetInterface *nif)
{
    DhcpClient *c = &clients[nif->index];
    if (!c->enabled)
        return;
    c->link_down = false;
    if (!DhcpIsBound(nif) || !DhcpLeaseValid(c)) {
        nif->ip = nif->netmask = nif->gateway = nif->dns = 0;
        DhcpRestart(c);
        return;
    }
    c->state = DHCP_REBOOTING;
    c->xid = NetNowMs();
    c->start_ms = NetNowMs();
    c->retries = 0;
    DhcpSendReboot(c);
}

static void DhcpClientTick(DhcpClient *c)
{
    uint32_t now = NetNowMs();

    if (c->link_down)
        return;

    switch (c->state) {
    case DHCP_INIT:
    case DHCP_PROBING:
        return; /* ACD runs on ArpTick's clock */

    case DHCP_SELECTING:
    case DHCP_REQUESTING:
        if ((int32_t)(now - c->sent_ms) < DHCP_TIMEOUT_MS)
            return; /* current attempt still has time */
        if (c->retries >= DHCP_MAX_RETRIES) {
            DhcpRestart(c); /* gave up: back to square one */
            return;
        }
        c->retries++;
        if (c->state == DHCP_SELECTING)
            DhcpSendDiscover(c);
        else
            DhcpSendRequest(c);
        return;

    case DHCP_BOUND: {
        if (c->lease_secs == DHCP_LEASE_INFINITE)
            return; /* permanent lease, never renew */
        uint32_t held = (now - c->bound_ms) / 1000;
        if (held >= c->t2_secs) { /* T1 missed, broadcast to anyone */
            c->state = DHCP_REBINDING;
            DhcpSendRenew(c, BROADCAST_IP);
        } else if (held >= c->t1_secs) { /* T1: unicast to our server */
            c->state = DHCP_RENEWING;
            DhcpSendRenew(c, c->server_id);
        }
        return;
    }

    case DHCP_RENEWING:
    case DHCP_REBINDING: {
        uint32_t held = (now - c->bound_ms) / 1000;
        if (held >= c->lease_secs) { /* lease fully expired: drop it */
            c->nif->ip = c->nif->netmask = c->nif->gateway = c->nif->dns = 0;
            DhcpRestart(c);
            return;
        }
        if (c->state == DHCP_RENEWING && held >= c->t2_secs)
            c->state = DHCP_REBINDING; /* escalate from unicast to broadcast */
        if ((int32_t)(now - c->sent_ms) >= DHCP_RENEW_RETRY_MS)
            DhcpSendRenew(c, c->state == DHCP_RENEWING ? c->server_id : BROADCAST_IP);
        return;
    }

    case DHCP_REBOOTING:
        if ((int32_t)(now - c->sent_ms) < DHCP_TIMEOUT_MS)
            return;
        if (c->retries < DHCP_MAX_RETRIES) {
            c->retries++;
            DhcpSendReboot(c);
            return;
        }
        LOG_INFO(LOG_TAG, "no DHCP reply for %u.%u.%u.%u after link up, starting over",
                 IP4(c->nif->ip));
        c->nif->ip = c->nif->netmask = c->nif->gateway = c->nif->dns = 0;
        DhcpRestart(c);
        return;
    }
}

void DhcpTick(void)
{
    for (int i = 0; i < NETIF_MAX; i++)
        if (clients[i].enabled)
            DhcpClientTick(&clients[i]);
}
