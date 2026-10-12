#ifndef NETD_SESSION_H
#define NETD_SESSION_H

#include <net/protocols/socket.h>
#include <types.h>
#include <zuzu/zuzu.h>

#define SESSION_MAX 30

typedef struct {
    bool in_use;
    Marker owner;
    Handle live;
    Handle app_event;
    uint32_t app_bit;
    bool dirty;
} Session;

Err SessionInit(Handle doorbell_event);

/* True when the call was a socket command and has been answered. */
bool SessionHandle(const PortWaitResult *call);

void SessionDoorbell(EventWord bits);
void SessionReapDead(void);

#endif
