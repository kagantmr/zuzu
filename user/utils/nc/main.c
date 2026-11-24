#include <net/socket.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util/stdio_core.h>
#include <ztask/ztask.h>
#include <zuzu/zuzu.h>

/** @brief Poll interval, in ms, at which main checks whether a worker has finished. */
#define NC_POLL_MS 100
/** @brief Exit status StdioRead() ends a task with on Ctrl-C. */
#define NC_STATUS_INTERRUPTED 130

/** @brief Where the socket is connected, shared by both workers. */
static struct {
    int sock;     /**< Open socket number. */
    bool udp;     /**< Whether it is a UDP socket. */
    Ipv4Addr ip;  /**< Peer address. */
    NetPort port; /**< Peer port. */
} g_nc;

/** @brief Set by a worker when the connection is over and nc should exit. */
static volatile bool g_done;

/** @brief Parse a decimal port 1-65535 into @p out; false on anything else. */
static bool PortParse(const char *s, NetPort *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 1 || v > 65535)
        return false;
    *out = (NetPort)v;
    return true;
}

/** @brief Send @p len bytes to the peer; false if the connection failed. */
static bool NcSend(const uint8_t *buf, uint32_t len)
{
    while (len > 0) {
        if (g_nc.udp) {
            uint16_t chunk = len > SOCK_DGRAM_MAX ? SOCK_DGRAM_MAX : (uint16_t)len;
            Err rc = SockSendTo(g_nc.sock, g_nc.ip, g_nc.port, buf, chunk);
            if (rc == ERR_BUFFULL) {
                SockWait(10);
                continue;
            }
            if (rc != ZUZU_OK)
                return false;
            buf += chunk;
            len -= chunk;
        } else {
            int n = SockSend(g_nc.sock, buf, len);
            if (n <= 0)
                return false;
            buf += n;
            len -= (uint32_t)n;
        }
    }
    return true;
}

/** @brief Forward what is typed to the peer until end of input or a send error. */
static void TtyWorker(void *arg)
{
    (void)arg;
    uint8_t buf[1024];
    for (;;) {
        int n = StdioRead(buf, sizeof(buf));
        if (n <= 0)
            break;
        if (!NcSend(buf, (uint32_t)n)) {
            g_done = true;
            return;
        }
    }
    if (g_nc.udp)
        g_done = true;
}

/** @brief Print what the peer sends until it closes the connection or an error occurs. */
static void SocketWorker(void *arg)
{
    (void)arg;
    uint8_t buf[2048];
    while (!g_done) {
        int n = g_nc.udp ? SockRecvFrom(g_nc.sock, buf, sizeof(buf), NULL, NULL, TIMEOUT_INFINITE)
                         : SockRecv(g_nc.sock, buf, sizeof(buf), TIMEOUT_INFINITE);
        if (n < 0 || (n == 0 && !g_nc.udp))
            break;
        if (n > 0)
            StdioWrite(1, buf, (size_t)n);
    }
    g_done = true;
}

/** @brief Print the usage line. */
static void Usage(void) { printf("usage: nc [-u] destination port\n"); }

int main(int argc, char **argv)
{
    bool have_ip = false, have_port = false;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (!strcmp(argv[i], "-u")) {
                g_nc.udp = true;
                continue;
            }
            printf("nc: unsupported flag %s\n", argv[i]);
            return ERR_BADARG;
        }
        if (!have_ip && Ipv4ToBytes(argv[i], &g_nc.ip))
            have_ip = true;
        else if (!have_port && PortParse(argv[i], &g_nc.port))
            have_port = true;
        else {
            printf("nc: bad argument %s\n", argv[i]);
            Usage();
            return ERR_BADARG;
        }
    }
    if (!have_ip || !have_port) {
        Usage();
        return ERR_BADARG;
    }

    g_nc.sock = SockOpen(g_nc.udp ? SOCK_KIND_UDP : SOCK_KIND_TCP);
    if (g_nc.sock < 0) {
        printf("nc: cannot open a socket: %s\n", StrToError((Err)g_nc.sock));
        return g_nc.sock;
    }
    Err rc = g_nc.udp ? SockBind(g_nc.sock, 0)
                      : SockConnect(g_nc.sock, g_nc.ip, g_nc.port, TIMEOUT_INFINITE);
    if (rc != ZUZU_OK) {
        printf("nc: %s failed: %s\n", g_nc.udp ? "bind" : "connect", StrToError(rc));
        SockClose(g_nc.sock);
        return rc;
    }

    ZTask *tty = ZTaskSpawn(TtyWorker, NULL, 0);
    if (PtrIsErr(tty)) {
        printf("nc: thread spawn failed: %s\n", StrToError((Err)(intptr_t)tty));
        SockClose(g_nc.sock);
        return (Err)(intptr_t)tty;
    }
    ZTask *rx = ZTaskSpawn(SocketWorker, NULL, 0);
    if (PtrIsErr(rx)) {
        printf("nc: thread spawn failed: %s\n", StrToError((Err)(intptr_t)rx));
        TaskKill(tty->handle);
        ZTaskJoin(tty, TIMEOUT_INFINITE, NULL);
        SockClose(g_nc.sock);
        return (Err)(intptr_t)rx;
    }

    /* Finished means: the socket worker ended (peer closed, or a send failed), or the typing
     * side ended by Ctrl-C. Plain end of input (Ctrl-D) only stops the sending; for TCP we keep
     * printing until the peer closes. */
    bool tty_joined = false;
    while (!g_done) {
        if (!tty_joined) {
            int32_t status = 0;
            Err jr = ZTaskJoin(tty, NC_POLL_MS, &status);
            if (jr == ERR_TIMEOUT)
                continue;
            tty_joined = true;
            if (jr != ZUZU_OK || status == NC_STATUS_INTERRUPTED)
                break;
        } else {
            Sleep(NC_POLL_MS);
        }
    }

    TaskKill(rx->handle);
    ZTaskJoin(rx, TIMEOUT_INFINITE, NULL);
    if (!tty_joined) {
        TaskKill(tty->handle);
        ZTaskJoin(tty, TIMEOUT_INFINITE, NULL);
    }
    SockClose(g_nc.sock);
    return ZUZU_OK;
}
