#include "dhcp.h"
#include "../common/globals.h"
#include "../link/arp.h"
#include "../transport/port.h"
#include "../transport/udp.h"
#include <convert.h>
#include <stddef.h>
#include <util/log.h>

/* RFC 2131 caps the safe DHCP message size at 576 bytes; our ACKs are far
   smaller. Big enough to stash one while ACD runs against the offered IP. */
#define DHCP_ACK_BUFSZ 576

static struct {
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
    DhcpBoundCb on_bound;            /* first-bind notification, may be NULL */
    bool notified;                   /* on_bound already fired? */
    uint8_t ack_buf[DHCP_ACK_BUFSZ]; /* ACK held while ACD probes offered_ip */
    uint16_t ack_len;                /* bytes valid in ack_buf */
} dhcp;

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

static size_t DhcpBuild(uint8_t *pkt)
{
    DhcpMsg *m = (DhcpMsg *)pkt;
    memset(m, 0, sizeof(DhcpMsg));
    m->op = DHCP_OP_REQUEST;
    m->htype = DHCP_HTYPE_ETH;
    m->hlen = 6;
    m->xid = dhcp.xid;
    memcpy(m->chaddr, netif.mac, 6);
    m->flags = htons(DHCP_FLAG_BROADCAST);
    m->cookie = htonl(DHCP_MAGIC_COOKIE);
    uint32_t elapsed = (NetNowMs() - dhcp.start_ms) / 1000;
    if (elapsed > 0xFFFF)
        elapsed = 0xFFFF;
    m->secs = htons((uint16_t)elapsed);
    return sizeof(DhcpMsg);
}

static __attribute__((cold)) int DhcpSendDiscover(void)
{
    uint8_t buf[sizeof(DhcpMsg) + 64];

    size_t len = DhcpBuild(buf);

    len += DhcpOptU8(buf + len, DHCP_OPT_MSGTYPE, DHCP_DISCOVER);
    static const uint8_t params[] = {DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    len += DhcpOptRaw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = UdpSend(BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK) {
        dhcp.state = DHCP_SELECTING;
        dhcp.sent_ms = NetNowMs();
    }
    return rc;
}

static __attribute__((cold)) int DhcpSendRequest(void)
{
    uint8_t buf[sizeof(DhcpMsg) + 64];

    size_t len = DhcpBuild(buf);

    len += DhcpOptU8(buf + len, DHCP_OPT_MSGTYPE, DHCP_REQUEST);
    len += DhcpOptU32(buf + len, DHCP_OPT_REQ_IP, dhcp.offered_ip);
    len += DhcpOptU32(buf + len, DHCP_OPT_SERVER_ID, dhcp.server_id);

    static const uint8_t params[] = {DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    len += DhcpOptRaw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = UdpSend(BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK) {
        dhcp.state = DHCP_REQUESTING;
        dhcp.sent_ms = NetNowMs();
    }
    return rc;
}

/* REQUEST for an existing lease (RENEWING/REBINDING). Unlike the selecting
   REQUEST, ciaddr carries our current address and we omit options 50/54
   (RFC 2131 4.3.2). RENEWING unicasts to the server; REBINDING broadcasts. */
static __attribute__((cold)) int DhcpSendRenew(Ipv4Addr dst)
{
    uint8_t buf[sizeof(DhcpMsg) + 64];

    size_t len = DhcpBuild(buf);
    DhcpMsg *m = (DhcpMsg *)buf;
    m->ciaddr = netif.ip; /* raw; proves which lease we're renewing */
    m->flags = 0;         /* we hold an IP, so accept a unicast reply */

    len += DhcpOptU8(buf + len, DHCP_OPT_MSGTYPE, DHCP_REQUEST);

    static const uint8_t params[] = {DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
    len += DhcpOptRaw(buf + len, DHCP_OPT_PARAM_LIST, params, sizeof(params));

    buf[len++] = DHCP_OPT_END;

    int rc = UdpSend(dst, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
    if (rc == ZUZU_OK)
        dhcp.sent_ms = NetNowMs();
    return rc;
}

/* DHCPDECLINE (RFC 2131 4.4.4): tell the server the offered address is already
   in use so it won't hand it out again, then restart the exchange. Broadcast,
   ciaddr 0, carrying the declined address (option 50) and the server id. */
static __attribute__((cold)) int DhcpSendDecline(void)
{
    uint8_t buf[sizeof(DhcpMsg) + 64];

    size_t len = DhcpBuild(buf);

    len += DhcpOptU8(buf + len, DHCP_OPT_MSGTYPE, DHCP_DECLINE);
    len += DhcpOptU32(buf + len, DHCP_OPT_REQ_IP, dhcp.offered_ip);
    len += DhcpOptU32(buf + len, DHCP_OPT_SERVER_ID, dhcp.server_id);

    buf[len++] = DHCP_OPT_END;

    return UdpSend(BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, buf, len);
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

static __attribute__((cold)) void DhcpRestart(void)
{
    dhcp.state = DHCP_INIT;
    dhcp.xid = NetNowMs();
    dhcp.start_ms = NetNowMs();
    dhcp.retries = 0;
    DhcpSendDiscover();
}

/* Apply an ACK's parameters and (re)enter BOUND. Shared by the initial
   acquire and by renew/rebind, so the lease clock restarts every ACK. */
static __attribute__((cold)) void DhcpEnterBound(const uint8_t *data, uint16_t len)
{
    const DhcpMsg *m = (const DhcpMsg *)data;
    uint8_t optlen;
    const uint8_t *p;

    netif.ip = m->yiaddr; /* raw */

    p = DhcpFindOption(data, len, DHCP_OPT_SUBNET, &optlen);
    if (p && optlen == 4)
        memcpy(&netif.netmask, p, 4); /* raw */

    p = DhcpFindOption(data, len, DHCP_OPT_ROUTER, &optlen);
    if (p && optlen >= 4)
        memcpy(&netif.gateway, p, 4); /* raw, first of the list */

    p = DhcpFindOption(data, len, DHCP_OPT_DNS, &optlen);
    if (p && optlen >= 4)
        memcpy(&netif.dns, p, 4); /* raw, first of the list */

    dhcp.lease_secs = DHCP_DEFAULT_LEASE;
    p = DhcpFindOption(data, len, DHCP_OPT_LEASE, &optlen);
    if (p && optlen == 4) {
        uint32_t lease_net;
        memcpy(&lease_net, p, 4);
        dhcp.lease_secs = ntohl(lease_net); /* the one swap */
    }
    /* T1/T2 per RFC 2131: renew at half the lease, rebind at 7/8. */
    dhcp.t1_secs = dhcp.lease_secs / 2;
    dhcp.t2_secs = dhcp.lease_secs - dhcp.lease_secs / 8;

    dhcp.bound_ms = NetNowMs();
    dhcp.retries = 0;
    dhcp.state = DHCP_BOUND;
    LOG_INFO(LOG_TAG, "bound %u.%u.%u.%u lease %us", IP4(netif.ip), dhcp.lease_secs);

    if (!dhcp.notified) {
        dhcp.notified = true;
        if (dhcp.on_bound)
            dhcp.on_bound();
    }
}

/* ARP conflict-detection verdict for the offered IP. On a conflict, decline the
   lease and restart DORA so the server picks a different address; otherwise the
   stashed ACK is now safe to commit. */
static __attribute__((cold)) void DhcpAcdDone(bool conflict)
{
    if (conflict) {
        LOG_WARN(LOG_TAG, "%u.%u.%u.%u in use, declining lease", IP4(dhcp.offered_ip));
        DhcpSendDecline();
        DhcpRestart();
        return;
    }
    DhcpEnterBound(dhcp.ack_buf, dhcp.ack_len);
}

/* Stash the ACK (the UDP buffer won't outlive this call) and probe the offered
   address before committing it. DhcpAcdDone resumes once ACD reports back. */
static __attribute__((cold)) void DhcpBeginAcd(const uint8_t *data, uint16_t len)
{
    if (len > DHCP_ACK_BUFSZ)
        len = DHCP_ACK_BUFSZ;
    memcpy(dhcp.ack_buf, data, len);
    dhcp.ack_len = len;
    dhcp.state = DHCP_PROBING;
    LOG_DEBUG(LOG_TAG, "probing %u.%u.%u.%u before binding", IP4(dhcp.offered_ip));
    ArpAcdStart(dhcp.offered_ip, DhcpAcdDone);
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
    if (m->xid != dhcp.xid)
        return;

    uint8_t optlen;
    const uint8_t *p = DhcpFindOption(data, len, DHCP_OPT_MSGTYPE, &optlen);
    if (!p || optlen != 1)
        return;
    uint8_t msgtype = *p;

    bool renewing = (dhcp.state == DHCP_RENEWING || dhcp.state == DHCP_REBINDING);

    if (dhcp.state == DHCP_SELECTING && msgtype == DHCP_OFFER) {
        dhcp.offered_ip = m->yiaddr; /* raw, no swap */

        p = DhcpFindOption(data, len, DHCP_OPT_SERVER_ID, &optlen);
        if (!p || optlen != 4)
            return;                    /* OFFER without server id is unusable */
        memcpy(&dhcp.server_id, p, 4); /* raw */

        DhcpSendRequest(); /* it sets state = REQUESTING */

    } else if ((dhcp.state == DHCP_REQUESTING || renewing) && msgtype == DHCP_ACK) {
        /* refresh which server holds the lease (relevant after a rebind) */
        p = DhcpFindOption(data, len, DHCP_OPT_SERVER_ID, &optlen);
        if (p && optlen == 4)
            memcpy(&dhcp.server_id, p, 4);
        /* Initial assignment: ARP-probe the offered IP before committing it.
           Renew/rebind already own the address, so bind straight away. */
        if (renewing)
            DhcpEnterBound(data, len);
        else
            DhcpBeginAcd(data, len);

    } else if ((dhcp.state == DHCP_REQUESTING || renewing) && msgtype == DHCP_NAK) {
        DhcpRestart(); /* lease refused: start over */
    }
}

__attribute__((cold)) void DhcpInit(DhcpBoundCb on_bound)
{
    memset(&dhcp, 0, sizeof(dhcp));
    dhcp.on_bound = on_bound;
    netif.ip = netif.netmask = netif.gateway = netif.dns = 0;
    /* DHCP's client port is fixed at 68 (servers reply there), so claim it
       explicitly rather than allocating an ephemeral one. */
    PortReserve(DHCP_CLIENT_PORT);
    UdpBind(DHCP_CLIENT_PORT, DhcpRecv);
    DhcpRestart();
}

bool DhcpIsBound(void)
{
    return dhcp.state == DHCP_BOUND || dhcp.state == DHCP_RENEWING ||
           dhcp.state == DHCP_REBINDING; /* lease still valid while renewing */
}

void DhcpTick(void)
{
    uint32_t now = NetNowMs();

    switch (dhcp.state) {
    case DHCP_INIT:
        return; /* nothing in flight */

    case DHCP_PROBING:
        return; /* ACD runs on ArpTick's clock */

    case DHCP_SELECTING:
    case DHCP_REQUESTING:
        if ((int32_t)(now - dhcp.sent_ms) < DHCP_TIMEOUT_MS)
            return; /* current attempt still has time */
        if (dhcp.retries >= DHCP_MAX_RETRIES) {
            DhcpRestart(); /* gave up: back to square one */
            return;
        }
        dhcp.retries++;
        if (dhcp.state == DHCP_SELECTING)
            DhcpSendDiscover();
        else
            DhcpSendRequest();
        return;

    case DHCP_BOUND: {
        if (dhcp.lease_secs == DHCP_LEASE_INFINITE)
            return; /* permanent lease, never renew */
        uint32_t held = (now - dhcp.bound_ms) / 1000;
        if (held >= dhcp.t2_secs) { /* T1 missed, broadcast to anyone */
            dhcp.state = DHCP_REBINDING;
            DhcpSendRenew(BROADCAST_IP);
        } else if (held >= dhcp.t1_secs) { /* T1: unicast to our server */
            dhcp.state = DHCP_RENEWING;
            DhcpSendRenew(dhcp.server_id);
        }
        return;
    }

    case DHCP_RENEWING:
    case DHCP_REBINDING: {
        uint32_t held = (now - dhcp.bound_ms) / 1000;
        if (held >= dhcp.lease_secs) { /* lease fully expired: drop it */
            netif.ip = netif.netmask = netif.gateway = netif.dns = 0;
            DhcpRestart();
            return;
        }
        if (dhcp.state == DHCP_RENEWING && held >= dhcp.t2_secs)
            dhcp.state = DHCP_REBINDING; /* escalate from unicast to broadcast */
        if ((int32_t)(now - dhcp.sent_ms) >= DHCP_RENEW_RETRY_MS)
            DhcpSendRenew(dhcp.state == DHCP_RENEWING ? dhcp.server_id : BROADCAST_IP);
        return;
    }
    }
}
