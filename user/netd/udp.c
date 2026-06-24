#include "globals.h"
#include "txframe.h"
#include "udp.h"
#include "ip.h"
#include "eth.h"
#include <convert.h>
#include <mem.h>
#include <stddef.h>

typedef struct {
    uint16_t port; 
    udp_handler_t handler;
} udp_entry_t;

static udp_entry_t udp_table[UDP_MAX_TABLE];

/* Checksum over the UDP pseudo-header + the contiguous UDP segment (header and
   payload). For TX the header's checksum field must be 0 on entry; for RX it
   holds the sender's value and a valid segment folds back to 0. */
static uint16_t udp_checksum(ipv4_addr_t src_ip, ipv4_addr_t dst_ip,
                             const uint8_t *seg, uint16_t seg_len) {
    uint8_t pseudo[12];
    memcpy(&pseudo[0], &src_ip, 4);
    memcpy(&pseudo[4], &dst_ip, 4);
    pseudo[8]  = 0;
    pseudo[9]  = IP_PROTO_UDP;
    pseudo[10] = (uint8_t)(seg_len >> 8);
    pseudo[11] = (uint8_t)(seg_len & 0xFF);

    uint32_t accum = inet_csum_partial(pseudo, sizeof(pseudo), 0);
    accum = inet_csum_partial(seg, seg_len, accum);
    return inet_csum_fold(accum);
}

void udp_init(void) {
    for (size_t i = 0; i < UDP_MAX_TABLE; i++) {
        udp_table[i].port = 0;
        udp_table[i].handler = NULL;
    }
}

int  udp_bind(uint16_t port, udp_handler_t handler) {
    if (!port || !handler) {
        return ERR_NOPERM; // port 0 is reserved
    }
    int first_free = -1;
    for (size_t i = 0; i < UDP_MAX_TABLE; i++) {
        if (udp_table[i].port == port && udp_table[i].handler) {
            return ERR_DUPLICATE;
        }
        if (!udp_table[i].handler && first_free == -1) {
            first_free = i;
        }
    }

    if (first_free == -1) {
        return ERR_NOMEM;
    }

    udp_table[first_free].port = port;
    udp_table[first_free].handler = handler;
    return ZUZU_OK;

}

void udp_rx(void *data, uint16_t len, ipv4_addr_t src_ip, ipv4_addr_t dst_ip) {
    if (len < 8) {
        return;
    }
    udp_hdr_t *hdr = (udp_hdr_t *)data;
    uint16_t ulen = ntohs(hdr->length);
    if (ulen < 8 || len < ulen) {
        return;
    }
    if (hdr->checksum != 0) {
        /* A zero checksum means the sender opted out (legal in IPv4); otherwise
           verify it over the pseudo-header + segment and drop on mismatch. */
        if (udp_checksum(src_ip, dst_ip, (const uint8_t *)data, ulen) != 0) {
            return;
        }
    }
    // demux
    uint16_t dport = ntohs(hdr->dst_port);
    for (size_t i = 0; i < UDP_MAX_TABLE; i++) {
        if (udp_table[i].port == dport && udp_table[i].handler) {
            udp_table[i].handler(src_ip, ntohs(hdr->src_port), dport, (uint8_t *)data + 8, ulen - 8);
            break;
        }
    }
}

int  udp_tx(ipv4_addr_t dst_ip, uint16_t src_port, uint16_t dst_port,
            const uint8_t *payload, uint16_t payload_len)
{
    if (payload_len > UDP_MAX_PAYLOAD) {
        return ERR_OVERFLOW;
    }

    /* Reserve room for the Ethernet + IP + UDP headers in front of the payload,
       then copy the caller's payload once into its final position in the slot. */
    txframe_t f;
    int rc = txframe_init(&f, sizeof(eth_hdr_t) + sizeof(ip_header_t) + sizeof(udp_hdr_t));
    if (rc != ZUZU_OK)
        return rc;

    if (payload_len) {
        void *dst = txframe_append(&f, payload_len);
        if (!dst)
            return ERR_OVERFLOW;
        memcpy(dst, payload, payload_len);
    }

    udp_hdr_t *hdr = (udp_hdr_t *)txframe_prepend(&f, sizeof(udp_hdr_t));
    if (!hdr)
        return ERR_OVERFLOW;

    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dst_port);
    hdr->length   = htons((uint16_t)(sizeof(udp_hdr_t) + payload_len));
    hdr->checksum = 0; /* must be zero while computing the checksum */

    /* Checksum covers the pseudo-header and the whole contiguous segment. A
       computed value of 0 is transmitted as 0xFFFF so the receiver doesn't
       mistake it for "no checksum" (RFC 768). */
    uint16_t cs = htons(udp_checksum(netif.ip, dst_ip, txframe_data(&f), txframe_len(&f)));
    hdr->checksum = cs ? cs : 0xFFFF;

    return ip_send(&f, netif.ip, dst_ip, IP_PROTO_UDP);
}
