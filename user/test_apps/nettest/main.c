#include <net/protocols/socket.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define UDP_PORT 5000
#define TCP_PORT 7000

int main(void)
{
    int udp = SockOpen(SOCK_KIND_UDP);
    if (udp < 0) {
        UserspaceDebugLog("nettest: udp open failed rc=%d", udp);
        return udp;
    }
    Err rc = SockBind(udp, UDP_PORT);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("nettest: udp bind failed rc=%d", rc);
        return rc;
    }

    int listener = SockOpen(SOCK_KIND_TCP);
    if (listener < 0) {
        UserspaceDebugLog("nettest: tcp open failed rc=%d", listener);
        return listener;
    }
    rc = SockListen(listener, TCP_PORT, 4);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("nettest: tcp listen failed rc=%d", rc);
        return rc;
    }
    UserspaceDebugLog("nettest: udp echo on %d, tcp echo on %d", UDP_PORT, TCP_PORT);

    uint8_t buf[SOCK_DGRAM_MAX];
    int client = -1;
    for (;;) {
        bool busy = false;

        uint32_t ip;
        uint16_t port;
        int n = SockRecvFrom(udp, buf, sizeof(buf), &ip, &port, TIMEOUT_POLL);
        if (n >= 0) {
            busy = true;
            if (n >= 4 && memcmp(buf, "quit", 4) == 0)
                break;
            SockSendTo(udp, ip, port, buf, (uint16_t)n);
        }

        if (client < 0) {
            int c = SockAccept(listener, TIMEOUT_POLL);
            if (c >= 0) {
                client = c;
                busy = true;
                UserspaceDebugLog("nettest: tcp client accepted");
            }
        }

        if (client >= 0) {
            n = SockRecv(client, buf, sizeof(buf), TIMEOUT_POLL);
            if (n > 0) {
                busy = true;
                SockSend(client, buf, (uint32_t)n);
            } else if (n != ERR_TIMEOUT) {
                busy = true;
                UserspaceDebugLog("nettest: tcp client done rc=%d", n);
                SockClose(client);
                client = -1;
            }
        }

        if (!busy)
            SockWait(TIMEOUT_INFINITE);
    }

    if (client >= 0)
        SockClose(client);
    SockClose(listener);
    SockClose(udp);
    return 0;
}
