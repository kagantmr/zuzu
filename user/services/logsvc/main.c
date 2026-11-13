#include <stdio.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

Handle g_logsvc_port = -1, g_event = -1;

#define PORT_BIT 0

int main() {
    g_logsvc_port = CreatePort();
    g_event = CreateEvent();
    
    Bind(EVENT_PORT, g_event, g_logsvc_port, PORT_BIT);
    RegisterService("/svc/log", g_logsvc_port);
    Err rc = ZUZU_OK;
    rc = RegisterService("/svc/log", g_logsvc_port);
    if (rc != ZUZU_OK) {
        return rc;
    }

    while (1) {
        EventWaitResult res = WaitOnEvent(g_event, TIMEOUT_INFINITE, 0, false);
        if (res.status != ZUZU_OK) return res.status;
    }
}