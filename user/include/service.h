#ifndef ZUZU_SERVICE_H
#define ZUZU_SERVICE_H

#include <zuzu/ipcx.h>
#include <stdint.h>
#include <zuzu/protocols/nt_protocol.h>

// register_service("zusd") does:
// 1. _port_create()
// 2. _port_grant(port, NAMETABLE_PID) → nt_slot
// 3. DEN_MYDEN to find which den we're in
// 4. NT_REGISTER | (den << 8) with packed name + nt_slot
// returns the port handle, or -err
int32_t register_service(const char *name);

// lookup_service("zusd") waits until the service exists and returns the port handle, or -err on transport failure
int32_t lookup_service(const char *name);

#endif