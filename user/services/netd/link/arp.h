#ifndef NETD_ARP_H
#define NETD_ARP_H

#include "../common/globals.h"
#include "../common/txframe.h"
#include <stdbool.h>
#include <stdint.h>
#include <vector.h>

#define ARP_OPER_REQST 1
#define ARP_OPER_REPLY 2

typedef struct __attribute__((packed)) {
    uint16_t htype; // hardware type: 1 for Ethernet
    uint16_t ptype; // protocol type: 0x0800 for IPv4
    uint8_t hlen;   // hardware address length: 6 for MAC
    uint8_t plen;   // protocol address length: 4 for IPv4
    uint16_t oper;  // operation: 1 = request, 2 = reply
    uint8_t sha[6]; // sender hardware address (MAC)
    uint8_t spa[4]; // sender protocol address (IP)
    uint8_t tha[6]; // target hardware address (MAC)
    uint8_t tpa[4]; // target protocol address (IP)
} ArpPacket;

#define ARP_MAX_ENTRIES 64 /* bound the table itself (this is also fix #6) */
#define ARP_MAX_QUEUE 3    /* pending packets per unresolved IP */
#define ARP_MAX_PROBES 2
#define ARP_PROBE_MS 500
#define ARP_REACHABLE_MS 60000

/* RFC 5227 address-conflict detection: number of probes and their spacing.
   Total detection window is ACD_PROBE_NUM * ACD_PROBE_MS before an address is
   declared free. */
#define ACD_PROBE_NUM 3
#define ACD_PROBE_MS 1000

typedef enum { ARP_FREE = 0, ARP_INCOMPLETE, ARP_REACHABLE } ArpState;

typedef struct {
    uint8_t *data;
    uint16_t len;
    uint16_t ethertype;
} ArpPending;

typedef struct {
    Ipv4Addr ip;
    uint8_t mac[6];
    ArpState state;
    uint8_t probes;      /* requests sent while INCOMPLETE */
    uint32_t last_tx_ms; /* last probe time   */
    uint32_t expire_ms;  /* REACHABLE aging   */
    ArpPending queue[ARP_MAX_QUEUE];
    uint8_t qlen;
} ArpEntry;

void ArpInit(void);
void ArpLearn(Ipv4Addr ip, const uint8_t *mac_addr);
int ArpRx(uint8_t *data, uint16_t len);
int ArpRequest(Ipv4Addr ip);
/* Send a zero-copy frame to ip, resolving the MAC first. On a cache hit the
   frame's tx-ring slot is committed in place; otherwise the built bytes are
   copied into the pending queue and ARP resolution is triggered. The builder's
   slot is always either committed or abandoned by this call. */
void ArpSendFrame(Ipv4Addr ip, uint16_t ethertype, TxFrame *f);
/* Drive ARP retransmits and cache aging; call once per netd event-loop wake. */
void ArpTick(void);

/* RFC 5227 address-conflict detection. Probe `ip` with gratuitous ARP requests
   (sender protocol address 0.0.0.0, so caches are not polluted with a yet-to-be
   -claimed address) and report the verdict via on_result: true if another host
   already answers for `ip`, false if it appears free after ACD_PROBE_NUM probes.
   on_result fires asynchronously, driven by ArpTick / ArpRx. DHCP uses this to
   validate an offered lease before committing it. A new call replaces any probe
   already in flight. */
void ArpAcdStart(Ipv4Addr ip, void (*on_result)(bool conflict));

#endif /* NETD_ARP_H */
