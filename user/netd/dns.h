#ifndef DNS_H
#define DNS_H

#include <zuzu/types.h>
#include "globals.h"

typedef struct
{
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount, ancount, nscount, arcount;
} __attribute__((packed)) dns_hdr_t;

_Static_assert(sizeof(dns_hdr_t) == 12, "header size wrong");

#define DNS_MAX_TABLE 16
#define DNS_MAX_NAME 253
#define DNS_CLIENT_PORT 50000

#define DNS_TIMEOUT_MS 5000

#define DNS_PORT 53
#define RCODE_MASK 0x0F
#define DNS_TYPE_A 1
#define DNS_TYPE_CNAME 5
#define DNS_CLASS_IN 1
#define DNS_PTR_MASK 0xC0
#define DNS_FLAG_QR 0x8000 /* bit 15: set in responses */
#define DNS_FLAG_RD 0x0100 /* bit 8:  recursion desired */
#define DNS_FLAG_TC 0x0200 /* bit 9:  truncated, try over TCP */

typedef void (*dns_callback_t)(const char *name, ipv4_addr_t ip, int status);

void dns_init(void);
void dns_query(const char *name, dns_callback_t cb);
void dns_tick(void);

#endif