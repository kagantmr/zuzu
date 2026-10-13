#include "arp.h"
#include "../common/globals.h"
#include "eth.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/log.h>

#define LOG_TAG "netd"

static ArpEntry arp_table[ARP_MAX_ENTRIES];

/* In-flight RFC 5227 conflict detection. Only one tentative address is probed at
   a time (DHCP validates a single offered lease). */
static struct {
    bool active;
    Ipv4Addr ip;      /* tentative address under test */
    uint8_t probes;   /* probes sent so far */
    uint32_t next_ms; /* when to send the next probe / declare free */
    void (*on_result)(bool conflict);
} acd;

__attribute__((cold)) void ArpInit()
{
    memset(arp_table, 0, sizeof(arp_table)); /* every slot ARP_FREE */
    memset(&acd, 0, sizeof(acd));
}

/* An ACD probe is an ARP request with the sender protocol address left at
   0.0.0.0 (RFC 5227 2.1.1): it asks "who has ip" without claiming ip ourselves. */
static __attribute__((cold)) void ArpAcdProbe(Ipv4Addr ip)
{
    ArpPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.htype = htons(1);
    pkt.ptype = htons(ETH_TYPE_IP);
    pkt.hlen = 6;
    pkt.plen = 4;
    pkt.oper = htons(ARP_OPER_REQST);
    memcpy(pkt.sha, netif.mac, 6); /* our MAC; spa stays 0.0.0.0 */
    memcpy(pkt.tpa, &ip, 4);
    MacAddr dst_mac = BROADCAST_MAC;
    EthTx(dst_mac, ETH_TYPE_ARP, (uint8_t *)&pkt, sizeof(ArpPacket));
}

void ArpAcdStart(Ipv4Addr ip, void (*on_result)(bool conflict))
{
    acd.active = true;
    acd.ip = ip;
    acd.probes = 0;
    acd.next_ms = NetNowMs(); /* first probe goes out on the next ArpTick */
    acd.on_result = on_result;
}

/* End the probe and report the verdict. The callback may start a fresh probe
   (e.g. DHCP declining and re-running DORA), so snapshot and clear first. */
static void ArpAcdFinish(bool conflict)
{
    if (!acd.active)
        return;
    acd.active = false;
    void (*cb)(bool) = acd.on_result;
    if (cb)
        cb(conflict);
}

static ArpEntry *ArpFind(Ipv4Addr ip)
{
    for (int i = 0; i < ARP_MAX_ENTRIES; i++)
        if (arp_table[i].state != ARP_FREE && arp_table[i].ip == ip)
            return &arp_table[i];
    return NULL;
}

static void ArpFreeEntry(ArpEntry *e)
{
    for (uint8_t i = 0; i < e->qlen; i++)
        free(e->queue[i].data);
    memset(e, 0, sizeof(*e)); /* state -> ARP_FREE, qlen -> 0 */
}

static ArpEntry *ArpFindOrCreate(Ipv4Addr ip)
{
    ArpEntry *e = ArpFind(ip);
    if (e)
        return e;

    /* prefer a free slot */
    for (int i = 0; i < ARP_MAX_ENTRIES; i++) {
        if (arp_table[i].state == ARP_FREE) {
            e = &arp_table[i];
            memset(e, 0, sizeof(*e));
            e->ip = ip;
            e->state = ARP_INCOMPLETE;
            return e;
        }
    }

    /* table full: evict the REACHABLE entry closest to expiry. Never evict an
       in-flight INCOMPLETE resolution (it owns queued packets and a probe). */
    ArpEntry *victim = NULL;
    for (int i = 0; i < ARP_MAX_ENTRIES; i++) {
        if (arp_table[i].state == ARP_REACHABLE &&
            (!victim || (int32_t)(arp_table[i].expire_ms - victim->expire_ms) < 0))
            victim = &arp_table[i];
    }
    if (!victim)
        return NULL;

    ArpFreeEntry(victim);
    victim->ip = ip;
    victim->state = ARP_INCOMPLETE;
    return victim;
}

void ArpLearn(Ipv4Addr ip, const uint8_t *mac_addr)
{
    ArpEntry *e = ArpFindOrCreate(ip);
    if (!e)
        return; /* table saturated with pending resolutions */

    bool was_pending = (e->state != ARP_REACHABLE);
    memcpy(e->mac, mac_addr, 6);
    e->state = ARP_REACHABLE;
    e->expire_ms = NetNowMs() + ARP_REACHABLE_MS;

    if (was_pending && e->qlen) {
        /* flush everything that was waiting on this address */
        LOG_INFO(LOG_TAG, "learned %u.%u.%u.%u: flushing %u queued", IP4(ip), e->qlen);
        for (uint8_t i = 0; i < e->qlen; i++) {
            EthTx(e->mac, e->queue[i].ethertype, e->queue[i].data, e->queue[i].len);
            free(e->queue[i].data);
        }
        e->qlen = 0;
    }
}
void ArpSendFrame(Ipv4Addr ip, uint16_t ethertype, TxFrame *f)
{
    if (ip == BROADCAST_IP) {
        MacAddr bcast = BROADCAST_MAC;
        EthSendFrame(f, bcast, ethertype);
        return;
    }
    ArpEntry *e = ArpFindOrCreate(ip);
    if (!e)
        return; /* table full, drop (slot abandoned) */

    if (e->state == ARP_REACHABLE) { /* fast path: commit the slot in place */
        EthSendFrame(f, e->mac, ethertype);
        return;
    }

    /* Unresolved: we cannot hold a ring slot across an ARP round-trip, so copy
       the built bytes out into the pending queue and abandon the slot. */
    if (e->qlen < ARP_MAX_QUEUE) { /* bounded enqueue */
        uint16_t len = TxFrameLen(f);
        uint8_t *copy = malloc(len);
        if (copy) {
            memcpy(copy, TxFrameData(f), len);
            e->queue[e->qlen].data = copy;
            e->queue[e->qlen].len = len;
            e->queue[e->qlen].ethertype = ethertype;
            e->qlen++;
        }
    } else {
        LOG_WARN(LOG_TAG, "ARP queue full for %u.%u.%u.%u, dropping packet", IP4(ip));
    }

    if (e->probes == 0) { /* kick off resolution exactly once */
        ArpRequest(ip);
        e->probes = 1;
        e->last_tx_ms = NetNowMs();
        LOG_INFO(LOG_TAG, "ARP miss %u.%u.%u.%u: queued %u, probing", IP4(ip), e->qlen);
    }
}

void ArpTick(void)
{
    uint32_t now = NetNowMs();

    /* Drive in-flight address-conflict detection: space out the probes, then
       declare the address free once they all go unanswered. A conflicting reply
       short-circuits this from ArpRx. */
    if (acd.active && (int32_t)(now - acd.next_ms) >= 0) {
        if (acd.probes < ACD_PROBE_NUM) {
            ArpAcdProbe(acd.ip);
            acd.probes++;
            acd.next_ms = now + ACD_PROBE_MS;
            LOG_INFO(LOG_TAG, "ACD probe %u for %u.%u.%u.%u", acd.probes, IP4(acd.ip));
        } else {
            ArpAcdFinish(false); /* no answer: address is free */
        }
    }

    for (int i = 0; i < ARP_MAX_ENTRIES; i++) {
        ArpEntry *e = &arp_table[i];
        if (e->state == ARP_INCOMPLETE) {
            if ((int32_t)(now - e->last_tx_ms) >= ARP_PROBE_MS) {
                if (e->probes < ARP_MAX_PROBES) {
                    ArpRequest(e->ip);
                    e->probes++;
                    e->last_tx_ms = now;
                    LOG_INFO(LOG_TAG, "ARP retransmit %u.%u.%u.%u probe %u", IP4(e->ip), e->probes);
                } else {
                    LOG_WARN(LOG_TAG, "ARP gave up %u.%u.%u.%u, dropped %u queued", IP4(e->ip),
                             e->qlen);
                    ArpFreeEntry(e); /* frees queued packets too */
                }
            }
        } else if (e->state == ARP_REACHABLE) {
            if ((int32_t)(now - e->expire_ms) >= 0)
                ArpFreeEntry(e); /* age out */
        }
    }
}

int ArpRx(uint8_t *data, uint16_t len)
{
    Ipv4Addr ip = netif.ip;
    if (len < sizeof(ArpPacket))
        return ERR_MALFORMED;
    ArpPacket *pkt = (ArpPacket *)data;
    if (ntohs(pkt->htype) != 1 || ntohs(pkt->ptype) != ETH_TYPE_IP || pkt->hlen != 6 ||
        pkt->plen != 4) {
        return ERR_MALFORMED;
    }

    Ipv4Addr spa;
    memcpy(&spa, pkt->spa, 4);

    /* RFC 5227 conflict: while probing a tentative address, any packet from a
       different host that either sources from it (spa == acd.ip) or simultaneously
       probes it (spa == 0, tpa == acd.ip) means the address is taken. */
    if (acd.active && memcmp(pkt->sha, netif.mac, 6) != 0) {
        Ipv4Addr tpa;
        memcpy(&tpa, pkt->tpa, 4);
        if (spa == acd.ip || (spa == 0 && tpa == acd.ip)) {
            LOG_WARN(LOG_TAG, "ACD: %u.%u.%u.%u already in use", IP4(acd.ip));
            ArpAcdFinish(true);
        }
    }

    ArpLearn(spa, pkt->sha);

    uint16_t op = ntohs(pkt->oper);
    switch (op) {
    case (ARP_OPER_REQST): {
        static RateLimiter arp_reply_rl;
        if (memcmp(pkt->tpa, &ip, 4) == 0 && RateAllow(&arp_reply_rl, 16, 16)) {
            ArpPacket pkt_out;
            pkt_out.htype = htons(1);
            pkt_out.ptype = htons(ETH_TYPE_IP);
            pkt_out.hlen = 6;
            pkt_out.plen = 4;
            pkt_out.oper = htons(2);           // reply
            memcpy(pkt_out.sha, netif.mac, 6); // our MAC
            memcpy(pkt_out.spa, &ip, 4);       // our IP
            memcpy(pkt_out.tha, pkt->sha, 6);  // their MAC
            memcpy(pkt_out.tpa, pkt->spa, 4);  // their IP
            EthTx(pkt->sha, ETH_TYPE_ARP, (uint8_t *)&pkt_out, sizeof(ArpPacket));
        }
        break;
    }
    case (ARP_OPER_REPLY): {
        break;
    }
    }

    return ZUZU_OK;
}

__attribute__((cold)) int ArpRequest(Ipv4Addr ip)
{
    ArpPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.htype = htons(1);
    pkt.ptype = htons(ETH_TYPE_IP);
    pkt.hlen = 6;
    pkt.plen = 4;
    Ipv4Addr our_ip = netif.ip;
    pkt.oper = htons(1);           // request
    memcpy(pkt.sha, netif.mac, 6); // our MAC
    memcpy(pkt.spa, &our_ip, 4);   // our IP
    // broadcast frame, no need to set it
    memcpy(pkt.tpa, &ip, 4); // their IP
    MacAddr dst_mac = BROADCAST_MAC;
    EthTx(dst_mac, ETH_TYPE_ARP, (uint8_t *)&pkt, sizeof(ArpPacket));
    return ZUZU_OK;
}
