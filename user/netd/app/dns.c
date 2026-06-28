#include "dns.h"
#include "../common/globals.h"
#include <mem.h>
#include "../common/txframe.h"
#include "../transport/udp.h"
#include "../transport/port.h"
#include <convert.h>
#include <string.h>
#include <zuzu/log.h>
#include <stddef.h>

typedef struct
{
    uint16_t id;
    dns_callback_t cb;
    bool in_use;
    char name[DNS_MAX_NAME];   /* original name requested, reported to the callback */
    char qname[DNS_MAX_NAME];  /* name currently being queried (may be a CNAME target) */
    uint32_t sent_ms;          /* time of the most recent send, for the timeout check */
    uint8_t retries_left;      /* retransmits remaining for the current query */
    uint8_t cname_hops;        /* CNAME redirections followed so far (loop guard) */
} dns_entry_t;

static dns_entry_t dns_table[DNS_MAX_TABLE];
static uint16_t dns_next_id = 1;
static port_t dns_client_port; /* ephemeral source port, allocated in dns_init */

static __attribute__((cold)) int dns_send_query(uint16_t id, const char *name);

/* (Re)issue the query held in slot->qname under a fresh transaction ID and a
   full retransmit budget. Returns the underlying send result. */
static __attribute__((cold)) int dns_start(dns_entry_t *slot)
{
    slot->id = dns_next_id++;
    slot->sent_ms = net_now_ms();
    slot->retries_left = DNS_MAX_RETRIES;
    return dns_send_query(slot->id, slot->qname);
}


static __attribute__((cold)) int dns_skip_name(const uint8_t *pkt, uint16_t len, uint16_t off) {
    while (1) {
        if (off >= len) return ERR_OVERFLOW;

        uint8_t byte = pkt[off];
        if (byte == 0) return off + 1;
        else if ((byte & 0xC0) == 0xC0) {
            if ((off + 1) < len) return off + 2;
            else return ERR_MALFORMED;
        }
        else off +=  1 + byte;
    }
}

/* Decode a (possibly compressed) name starting at `off` into `out` as a
   NUL-terminated dotted string. Compression pointers are followed, but only
   backwards and a bounded number of times, so a crafted packet can't loop us.
   Returns 0 on success or a negative error. */
static int dns_decode_name(const uint8_t *pkt, uint16_t len, uint16_t off,
                           char *out, size_t out_cap) {
    size_t outpos = 0;
    int jumps = 0;

    while (1) {
        if (off >= len) return ERR_MALFORMED;
        uint8_t b = pkt[off];

        if (b == 0) {
            break;
        } else if ((b & 0xC0) == 0xC0) {
            if ((uint16_t)(off + 1) >= len) return ERR_MALFORMED;
            uint16_t ptr = (uint16_t)(((b & 0x3F) << 8) | pkt[off + 1]);
            if (ptr >= off) return ERR_MALFORMED;       /* must point strictly back */
            if (++jumps > DNS_MAX_JUMPS) return ERR_MALFORMED;
            off = ptr;
        } else {
            uint8_t label = b;
            if (label > 63) return ERR_MALFORMED;
            if ((size_t)off + 1 + label > len) return ERR_MALFORMED;
            if (outpos) {
                if (outpos + 1 >= out_cap) return ERR_OVERFLOW;
                out[outpos++] = '.';
            }
            if (outpos + label >= out_cap) return ERR_OVERFLOW;
            memcpy(out + outpos, pkt + off + 1, label);
            outpos += label;
            off = (uint16_t)(off + 1 + label);
        }
    }

    out[outpos] = '\0';
    return 0;
}

void dns_tick(void) {
    uint32_t now = net_now_ms();
    for (int i = 0; i < DNS_MAX_TABLE; i++) {
        dns_entry_t *slot = &dns_table[i];
        if (!slot->in_use) continue;
        if ((int32_t)(now - slot->sent_ms) < DNS_TIMEOUT_MS) continue;

        if (slot->retries_left > 0) {
            /* Lost datagram? Resend the same query (same ID) and restart the clock. */
            slot->retries_left--;
            slot->sent_ms = now;
            int rc = dns_send_query(slot->id, slot->qname);
            if (rc != ZUZU_OK) {
                slot->cb(slot->name, 0, rc);
                slot->in_use = false;
            }
        } else {
            slot->cb(slot->name, 0, ERR_TIMEOUT);
            slot->in_use = false;
        }
    }
}

static __attribute__((cold)) void dns_recv(ipv4_addr_t src_ip, port_t src_port,
                     port_t dst_port, const uint8_t *data, uint16_t len)
{
    (void)src_ip; (void)src_port; (void)dst_port;
    if (len < sizeof(dns_hdr_t)) return;
    dns_hdr_t *h = (dns_hdr_t *)data;
    dns_entry_t *slot = NULL;

    uint16_t id = ntohs(h->id);
    for (int i = 0; i < DNS_MAX_TABLE; i++) {
        if (dns_table[i].in_use && dns_table[i].id == id) {
            slot = &dns_table[i];
            break;
        }
    }

    if (!slot) return;

    uint16_t flags = ntohs(h->flags);
    if (!(flags & DNS_FLAG_QR)) return;
    if (flags & RCODE_MASK) {
        // no need to distinguish RCODE error codes
        slot->cb(slot->name, 0, ERR_NOENT);
        slot->in_use = false;
        return;
    }

    int off = sizeof(dns_hdr_t);          // questions start at byte 12
    off = dns_skip_name(data, len, off);  // walk past the question's name
    if (off < 0) { slot->cb(slot->name, 0, ERR_NOENT); slot->in_use = false; return; }
    off += 4;                             // QTYPE(2) + QCLASS(2)

    uint16_t ancount = ntohs(h->ancount);
    char cname_target[DNS_MAX_NAME];
    bool have_cname = false;

    for (uint16_t a = 0; a < ancount; a++) {
        // 1. skip this RR's name
        off = dns_skip_name(data, len, off);
        if (off < 0) break;          // malformed

        // 2. the fixed fields
        if ((size_t)off + 10 > len) break;

        // 3. read TYPE, CLASS, RDLENGTH (unaligned, use memcpy)
        uint16_t type, class_, rdlength;
        memcpy(&type,     data + off,     2); type     = ntohs(type);
        memcpy(&class_,   data + off + 2, 2); class_   = ntohs(class_);
        memcpy(&rdlength, data + off + 8, 2); rdlength = ntohs(rdlength);

        // todo: copy ttl in

        // 4. bounds check the rdata
        if ((size_t)off + 10 + rdlength > len) break;

        // 5. is it the A record we want?
        if (type == DNS_TYPE_A && class_ == DNS_CLASS_IN && rdlength == 4) {
            ipv4_addr_t ip;
            memcpy(&ip, data + off + 10, 4);   // already network order
            slot->cb(slot->name, ip, ZUZU_OK);
            slot->in_use = false;
            return;
        }

        // 6. a CNAME points at another name; remember it in case the server
        //    didn't inline the A record, then chase it after this loop.
        if (type == DNS_TYPE_CNAME && class_ == DNS_CLASS_IN) {
            if (dns_decode_name(data, len, (uint16_t)(off + 10),
                                cname_target, sizeof(cname_target)) == 0)
                have_cname = true;
        }

        off += 10 + rdlength;
    }

    // No A record in this response. If we were handed an alias, follow it with
    // a fresh query (bounded, to defeat CNAME loops); otherwise the name has no
    // address for us.
    if (have_cname && slot->cname_hops < DNS_MAX_CNAME) {
        slot->cname_hops++;
        memcpy(slot->qname, cname_target, strlen(cname_target) + 1);
        int rc = dns_start(slot);
        if (rc != ZUZU_OK) {
            slot->cb(slot->name, 0, rc);
            slot->in_use = false;
        }
        return;
    }

    slot->cb(slot->name, 0, ERR_NOENT);
    slot->in_use = false;
    return;
}

__attribute__((cold)) void dns_init(void)
{
    for (int i = 0; i < DNS_MAX_TABLE; i++)
    {
        dns_table[i].in_use = false;
        dns_table[i].cb = NULL;
        memset(dns_table[i].name, 0, DNS_MAX_NAME);
        dns_table[i].sent_ms = 0;
    }
    dns_client_port = port_alloc();
    if (dns_client_port == 0) {
        LOG_ERROR(LOG_TAG, "no ephemeral port available for DNS");
        return;
    }
    udp_bind(dns_client_port, dns_recv);
}

static __attribute__((cold)) int encode_name(uint8_t *out, size_t cap, const char *name)
{
    size_t pos = 0;
    const char *p = name;

    while (*p)
    {
        const char *dot = p;
        while (*dot && *dot != '.')
            dot++;

        size_t len = (size_t)(dot - p);
        if (len == 0 || len > 63) // empty label or > max label len
            return -1;
        if (pos + 1 + len > cap) // length byte + label
            return -1;

        out[pos++] = (uint8_t)len;
        memcpy(&out[pos], p, len);
        pos += len;

        p = dot;
        if (*p == '.')
            p++; // skip separator
    }

    if (pos + 1 > cap)
        return -1;
    out[pos++] = 0; // root label

    return (int)pos;
}

static __attribute__((cold)) int dns_send_query(uint16_t id, const char *name)
{
    uint8_t pkt[sizeof(dns_hdr_t) + DNS_MAX_NAME + 1 + 4];
    size_t pos = sizeof(dns_hdr_t);
    dns_hdr_t *h = (dns_hdr_t *)pkt;
    h->id = htons(id);
    h->flags = htons(DNS_FLAG_RD);
    h->qdcount = htons(1);
    h->ancount = 0;
    h->nscount = 0;
    h->arcount = 0;

    int nlen = encode_name(pkt + pos, sizeof(pkt) - pos, name);
    if (nlen < 0)
        return ERR_MALFORMED;
    pos += nlen;
    /* QTYPE + QCLASS, big-endian */
    pkt[pos++] = 0;
    pkt[pos++] = DNS_TYPE_A;
    pkt[pos++] = 0;
    pkt[pos++] = DNS_CLASS_IN;

    return udp_tx(netif.dns, dns_client_port, DNS_PORT, pkt, pos);
}

__attribute__((cold)) void dns_query(const char *name, dns_callback_t cb)
{
    size_t nlen_host = strlen(name);
    if (nlen_host >= DNS_MAX_NAME)
    {
        if (cb)
            cb(name, 0, ERR_MALFORMED);
        return;
    }

    dns_entry_t *slot = NULL;
    for (int i = 0; i < DNS_MAX_TABLE; i++)
    {
        if (!dns_table[i].in_use)
        {
            slot = &dns_table[i];
            break;
        }
    }
    if (!slot)
    {
        if (cb)
            cb(name, 0, ERR_NOMEM);
        return;
    }

    slot->cb = cb;
    slot->in_use = true;
    slot->cname_hops = 0;
    memcpy(slot->name, name, nlen_host + 1);
    memcpy(slot->qname, name, nlen_host + 1);

    int rc = dns_start(slot);
    if (rc != ZUZU_OK)
    {
        slot->in_use = false; // unavoidable
        if (cb)
            cb(name, 0, rc);
        return;
    }
}