#ifndef ZUZU_SERVICE_H
#define ZUZU_SERVICE_H

#include <zuzu/ipcx.h>
#include <stdint.h>
#include <zuzu/protocols/nt_protocol.h>

// zservice_register("zusd") does:
// 1. _port_create()
// 2. _port_grant(port, NAMETABLE_PID) → nt_slot
// 3. DEN_MYDEN to find which den we're in
// 4. NT_REGISTER | (den << 8) with packed name + nt_slot
// returns the port handle, or -err
int32_t zservice_register(const char *name);

// zservice_lookup("zusd") → port handle in your table, or -err
int32_t zservice_lookup(const char *name);

#endif