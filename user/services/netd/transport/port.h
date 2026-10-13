#ifndef PORT_H
#define PORT_H

#include "../common/globals.h"
#include <stdbool.h>
#include <types.h>

/* Local UDP port allocator. Owns the namespace of source ports netd binds, so
   ephemeral clients (DNS) and protocol-mandated clients (DHCP's 68) never
   collide. This only tracks ownership; demux handlers still register via
   UdpBind(). */

/* IANA dynamic/ephemeral range (RFC 6335). */
#define PORT_EPHEMERAL_MIN 49152u
#define PORT_EPHEMERAL_MAX 65535u

void PortInit(void);

/* Claim a specific local port (for protocol-fixed ports like DHCP's 68).
   Returns false if the port is 0 or already held. */
bool PortReserve(NetPort port);

/* Claim an unused port from the ephemeral range. Returns 0 if none are free. */
NetPort PortAlloc(void);

/* Release a port previously taken via PortReserve()/PortAlloc(). */
void PortRelease(NetPort port);

#endif
