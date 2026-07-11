#ifndef PORT_H
#define PORT_H

#include <zuzu/types.h>
#include <stdbool.h>
#include "../common/globals.h"   /* port_t */

/* Local UDP port allocator. Owns the namespace of source ports netd binds, so
   ephemeral clients (DNS) and protocol-mandated clients (DHCP's 68) never
   collide. This only tracks ownership; demux handlers still register via
   udp_bind(). */

/* IANA dynamic/ephemeral range (RFC 6335). */
#define PORT_EPHEMERAL_MIN 49152u
#define PORT_EPHEMERAL_MAX 65535u

void port_init(void);

/* Claim a specific local port (for protocol-fixed ports like DHCP's 68).
   Returns false if the port is 0 or already held. */
bool port_reserve(port_t port);

/* Claim an unused port from the ephemeral range. Returns 0 if none are free. */
port_t port_alloc(void);

/* Release a port previously taken via port_reserve()/port_alloc(). */
void port_release(port_t port);

#endif
