#include <stdio.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

#define MAX_SESSIONS 15 /* doorbell bits 1..15, death bits 16..30; bit 0 is the port */

Handle g_port = -1, g_event = -1;
#define PORT_BIT 0

int main(void)
{

    g_port = CreatePort();
    g_event = CreateEvent();
    if (g_port < 0 || g_event < 0)
        return ERR_NOMEM;
    Err rc = Bind(EVENT_PORT, g_event, g_port, PORT_BIT);
    if (rc != ZUZU_OK) {

        return rc;
    }

    rc = RegisterService("/svc/tty", g_port);
    if (rc != ZUZU_OK)
        return rc;

    for (;;) {
        EventWaitResult w = WaitOnEvent(g_event, TIMEOUT_INFINITE, (1U << PORT_BIT), false);
        if (w.status == ZUZU_OK) {
            
        }

        for (;;) {
            PortWaitResult r = WaitOnPort(g_port, TIMEOUT_POLL);
            if (r.status != ZUZU_OK)
                break;
            // handle req
        }

        // service
    }
}