#include "port.h"
#include "user/services/netd/common/globals.h"
#include <stdint.h>

/* Sized to the demux table: every held port ends up bound there too. */
#define PORT_MAX_HELD 64

static NetPort held[PORT_MAX_HELD];
static NetPort next_ephemeral; /* rotating cursor, so freed ports aren't reused at once */

__attribute__((cold)) void PortInit(void)
{
    for (int i = 0; i < PORT_MAX_HELD; i++)
        held[i] = 0;
    next_ephemeral = PORT_EPHEMERAL_MIN;
}

static bool IsHeld(NetPort port)
{
    for (int i = 0; i < PORT_MAX_HELD; i++)
        if (held[i] == port)
            return true;
    return false;
}

/* Record ownership of an unheld port; false when the table is full. */
static bool hold(NetPort port)
{
    for (int i = 0; i < PORT_MAX_HELD; i++) {
        if (held[i] == 0) {
            held[i] = port;
            return true;
        }
    }
    return false;
}

__attribute__((cold)) bool PortReserve(NetPort port)
{
    if (port == 0 || IsHeld(port))
        return false;
    return hold(port);
}

__attribute__((cold)) NetPort PortAlloc(void)
{
    uint32_t span = PORT_EPHEMERAL_MAX - PORT_EPHEMERAL_MIN + 1;
    for (uint32_t i = 0; i < span; i++) {
        NetPort port = next_ephemeral;
        next_ephemeral = (next_ephemeral == PORT_EPHEMERAL_MAX) ? PORT_EPHEMERAL_MIN
                                                                : (uint16_t)(next_ephemeral + 1);
        if (!IsHeld(port) && hold(port))
            return port;
    }
    return 0; /* every ephemeral port is held */
}

__attribute__((cold)) void PortRelease(NetPort port)
{
    for (int i = 0; i < PORT_MAX_HELD; i++) {
        if (held[i] == port) {
            held[i] = 0;
            return;
        }
    }
}
