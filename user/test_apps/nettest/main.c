#include <net/protocols/socket.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>
#include <string.h>

#define ECHO_PORT 5000

int main(void)
{
    int fd = SockOpen(SOCK_KIND_UDP);
    if (fd < 0) {
        UserspaceDebugLog("nettest: SockOpen failed rc=%d", fd);
        return fd;
    }

    Err rc = SockBind(fd, ECHO_PORT);
    if (rc != ZUZU_OK) {
        UserspaceDebugLog("nettest: SockBind failed rc=%d", rc);
        return rc;
    }
    UserspaceDebugLog("nettest: echoing udp on port %d", ECHO_PORT);

    uint8_t buf[SOCK_DGRAM_MAX];
    for (;;) {
        uint32_t ip;
        uint16_t port;
        int n = SockRecvFrom(fd, buf, sizeof(buf), &ip, &port, TIMEOUT_INFINITE);
        if (n < 0) {
            UserspaceDebugLog("nettest: recv failed rc=%d", n);
            if (n == ERR_NOTCONN)
                return n;
            continue;
        }
        if (n >= 4 && memcmp(buf, "quit", 4) == 0) {
            SockClose(fd);
            return 0;
        }
        rc = SockSendTo(fd, ip, port, buf, (uint16_t)n);
        if (rc != ZUZU_OK)
            UserspaceDebugLog("nettest: send failed rc=%d", rc);
    }
    return 0;
}
