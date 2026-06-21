#include "arp.h"
#include "eth.h"
#include "globals.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <mem.h>
#include <zuzu/syspage.h>
#include <zuzu/log.h>

#define LOG_TAG "netd"

/* expand a network-order ipv4_addr_t to four %u args (a.b.c.d) */
#define IP4(x) (unsigned)((x) & 0xff), (unsigned)(((x) >> 8) & 0xff), \
               (unsigned)(((x) >> 16) & 0xff), (unsigned)(((x) >> 24) & 0xff)

static arp_entry_t arp_table[ARP_MAX_ENTRIES];

/* Monotonic millisecond clock from the read-only syspage (no syscall). */
static uint32_t now_ms(void) {
    syspage_t *sp = (syspage_t *)SYSPAGE;
    uint32_t hz = sp->tick_hz ? sp->tick_hz : 1000u;
    return (uint32_t)((sp->uptime_ticks * 1000ull) / hz);
}

void arp_init() {
    memset(arp_table, 0, sizeof(arp_table)); /* every slot ARP_FREE */
}

static arp_entry_t *arp_find(ipv4_addr_t ip) {
    for (int i = 0; i < ARP_MAX_ENTRIES; i++)
        if (arp_table[i].state != ARP_FREE && arp_table[i].ip == ip)
            return &arp_table[i];
    return NULL;
}

static void arp_free_entry(arp_entry_t *e) {
    for (uint8_t i = 0; i < e->qlen; i++)
        free(e->queue[i].data);
    memset(e, 0, sizeof(*e)); /* state -> ARP_FREE, qlen -> 0 */
}

static arp_entry_t *arp_find_or_create(ipv4_addr_t ip) {
    arp_entry_t *e = arp_find(ip);
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
    arp_entry_t *victim = NULL;
    for (int i = 0; i < ARP_MAX_ENTRIES; i++) {
        if (arp_table[i].state == ARP_REACHABLE &&
            (!victim || (int32_t)(arp_table[i].expire_ms - victim->expire_ms) < 0))
            victim = &arp_table[i];
    }
    if (!victim)
        return NULL;

    arp_free_entry(victim);
    victim->ip = ip;
    victim->state = ARP_INCOMPLETE;
    return victim;
}

void arp_learn(ipv4_addr_t ip, const uint8_t *mac_addr) {
    arp_entry_t *e = arp_find_or_create(ip);
    if (!e)
        return; /* table saturated with pending resolutions */

    bool was_pending = (e->state != ARP_REACHABLE);
    memcpy(e->mac, mac_addr, 6);
    e->state = ARP_REACHABLE;
    e->expire_ms = now_ms() + ARP_REACHABLE_MS;

    if (was_pending && e->qlen) {
        /* flush everything that was waiting on this address */
        LOG_INFO(LOG_TAG, "learned %u.%u.%u.%u: flushing %u queued", IP4(ip), e->qlen);
        for (uint8_t i = 0; i < e->qlen; i++) {
            eth_tx(e->mac, e->queue[i].ethertype, e->queue[i].data, e->queue[i].len);
            free(e->queue[i].data);
        }
        e->qlen = 0;
    }
}

int arp_lookup(ipv4_addr_t ip, uint8_t *mac_out) {
    arp_entry_t *e = arp_find(ip);
    if (e && e->state == ARP_REACHABLE) {
        memcpy(mac_out, e->mac, 6);
        return ZUZU_OK;
    }
    return ERR_NOENT;
}

void arp_send_or_queue(ipv4_addr_t ip, uint16_t ethertype, uint8_t *data, uint16_t len) {
    arp_entry_t *e = arp_find_or_create(ip);
    if (!e)
        return; /* table full, drop */

    if (e->state == ARP_REACHABLE) { /* fast path / resolved while queued */
        eth_tx(e->mac, ethertype, data, len);
        return;
    }

    if (e->qlen < ARP_MAX_QUEUE) { /* bounded enqueue */
        uint8_t *copy = malloc(len);
        if (copy) {
            memcpy(copy, data, len);
            e->queue[e->qlen].data = copy;
            e->queue[e->qlen].len = len;
            e->queue[e->qlen].ethertype = ethertype;
            e->qlen++;
        }
    } else {
        LOG_WARN(LOG_TAG, "ARP queue full for %u.%u.%u.%u, dropping packet", IP4(ip));
    }

    if (e->probes == 0) { /* kick off resolution exactly once */
        arp_request(ip);
        e->probes = 1;
        e->last_tx_ms = now_ms();
        LOG_INFO(LOG_TAG, "ARP miss %u.%u.%u.%u: queued %u, probing", IP4(ip), e->qlen);
    }
}

void arp_tick(void) {
    uint32_t now = now_ms();
    for (int i = 0; i < ARP_MAX_ENTRIES; i++) {
        arp_entry_t *e = &arp_table[i];
        if (e->state == ARP_INCOMPLETE) {
            if ((int32_t)(now - e->last_tx_ms) >= ARP_PROBE_MS) {
                if (e->probes < ARP_MAX_PROBES) {
                    arp_request(e->ip);
                    e->probes++;
                    e->last_tx_ms = now;
                    LOG_INFO(LOG_TAG, "ARP retransmit %u.%u.%u.%u probe %u",
                             IP4(e->ip), e->probes);
                } else {
                    LOG_WARN(LOG_TAG, "ARP gave up %u.%u.%u.%u, dropped %u queued",
                             IP4(e->ip), e->qlen);
                    arp_free_entry(e); /* frees queued packets too */
                }
            }
        } else if (e->state == ARP_REACHABLE) {
            if ((int32_t)(now - e->expire_ms) >= 0)
                arp_free_entry(e); /* age out */
        }
    }
}

int arp_rx(uint8_t *data, uint16_t len) {
    ipv4_addr_t ip = ZUZU_IP;
    if (len < sizeof(arp_packet_t))
        return ERR_MALFORMED;
    arp_packet_t *pkt = (arp_packet_t *)data;
    if (ntohs(pkt->htype) != 1 || ntohs(pkt->ptype) != ETH_TYPE_IP ||
        pkt->hlen != 6 || pkt->plen != 4) {
        return ERR_MALFORMED;
    }

    ipv4_addr_t spa;
    memcpy(&spa, pkt->spa, 4);
    arp_learn(spa, pkt->sha);

    uint16_t op = ntohs(pkt->oper);
    switch (op) {
        case (ARP_OPER_REQST): {
            if (memcmp(pkt->tpa, &ip, 4) == 0) {
                arp_packet_t pkt_out;
                pkt_out.htype = htons(1);
                pkt_out.ptype = htons(ETH_TYPE_IP);
                pkt_out.hlen  = 6;
                pkt_out.plen  = 4;
                pkt_out.oper  = htons(2);          // reply
                memcpy(pkt_out.sha, mac, 6);       // our MAC
                memcpy(pkt_out.spa, &ip, 4);       // our IP
                memcpy(pkt_out.tha, pkt->sha, 6);  // their MAC
                memcpy(pkt_out.tpa, pkt->spa, 4);  // their IP
                eth_tx(pkt->sha, ETH_TYPE_ARP, (uint8_t *)&pkt_out, sizeof(arp_packet_t));
            }
            break;
        }
        case (ARP_OPER_REPLY): {
            break;
        }
    }

    return ZUZU_OK;
}

int arp_request(ipv4_addr_t ip) {
    arp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.htype = htons(1);
    pkt.ptype = htons(ETH_TYPE_IP);
    pkt.hlen = 6;
    pkt.plen = 4;
    ipv4_addr_t our_ip = ZUZU_IP;
    pkt.oper = htons(1); // request
    memcpy(pkt.sha, mac, 6);       // our MAC
    memcpy(pkt.spa, &our_ip, 4);       // our IP
    // broadcast frame, no need to set it
    memcpy(pkt.tpa, &ip, 4);  // their IP
    mac_addr_t dst_mac = BROADCAST_MAC;
    eth_tx(dst_mac, ETH_TYPE_ARP, (uint8_t *)&pkt, sizeof(arp_packet_t));
    return ZUZU_OK;
}
