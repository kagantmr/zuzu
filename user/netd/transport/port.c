#include "port.h"

/* Sized to the demux table: every held port ends up bound there too. */
#define PORT_MAX_HELD 64

static port_t held[PORT_MAX_HELD];
static port_t next_ephemeral; /* rotating cursor, so freed ports aren't reused at once */

__attribute__((cold)) void port_init(void) {
    for (int i = 0; i < PORT_MAX_HELD; i++)
        held[i] = 0;
    next_ephemeral = PORT_EPHEMERAL_MIN;
}

static bool is_held(port_t port) {
    for (int i = 0; i < PORT_MAX_HELD; i++)
        if (held[i] == port)
            return true;
    return false;
}

/* Record ownership of an unheld port; false when the table is full. */
static bool hold(port_t port) {
    for (int i = 0; i < PORT_MAX_HELD; i++) {
        if (held[i] == 0) {
            held[i] = port;
            return true;
        }
    }
    return false;
}

__attribute__((cold)) bool port_reserve(port_t port) {
    if (port == 0 || is_held(port))
        return false;
    return hold(port);
}

__attribute__((cold)) port_t port_alloc(void) {
    uint32_t span = PORT_EPHEMERAL_MAX - PORT_EPHEMERAL_MIN + 1;
    for (uint32_t i = 0; i < span; i++) {
        port_t port = next_ephemeral;
        next_ephemeral = (next_ephemeral == PORT_EPHEMERAL_MAX)
                       ? PORT_EPHEMERAL_MIN : (uint16_t)(next_ephemeral + 1);
        if (!is_held(port) && hold(port))
            return port;
    }
    return 0; /* every ephemeral port is held */
}

__attribute__((cold)) void port_release(port_t port) {
    for (int i = 0; i < PORT_MAX_HELD; i++) {
        if (held[i] == port) {
            held[i] = 0;
            return;
        }
    }
}
