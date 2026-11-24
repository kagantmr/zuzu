#include <net/socket.h>
#include <stdint.h>
#include <string.h>
#include <zuzu/udbg.h>
#include <zuzu/zuzu.h>

#define PORT_TCP_RX 5201
#define PORT_TCP_TX 5202
#define PORT_UDP_ECHO 5203
#define PORT_UDP_RX 5204

/** @brief Counters for the udp-rx test, reset by a "done" datagram. */
typedef struct {
    uint32_t received;
    uint32_t highest;
} UdpRxState;

#define CHUNK_BYTES 8192u

/** @brief Read exactly @p len bytes from @p sock; false on error or end of stream. */
static bool RecvExact(int sock, void *buf, uint32_t len)
{
    uint8_t *p = buf;
    uint32_t got = 0;
    while (got < len) {
        int r = SockRecv(sock, p + got, len - got, TIMEOUT_INFINITE);
        if (r <= 0)
            return false;
        got += (uint32_t)r;
    }
    return true;
}

/** @brief Read a u64 N, read and discard N bytes, reply with the u64 count received. */
static void ServeTcpRx(int client)
{
    static uint8_t buf[CHUNK_BYTES];
    uint64_t n;
    if (!RecvExact(client, &n, sizeof(n)))
        return;
    uint64_t got = 0;
    while (got < n) {
        int r = SockRecv(client, buf, sizeof(buf), TIMEOUT_INFINITE);
        if (r <= 0)
            break;
        got += (uint32_t)r;
    }
    SockSend(client, &got, sizeof(got));
}

/** @brief Read a u64 N and send N bytes back. */
static void ServeTcpTx(int client)
{
    static uint8_t buf[CHUNK_BYTES];
    uint64_t n;
    if (!RecvExact(client, &n, sizeof(n)))
        return;
    uint64_t sent = 0;
    while (sent < n) {
        uint32_t len = n - sent < sizeof(buf) ? (uint32_t)(n - sent) : (uint32_t)sizeof(buf);
        if (SockSend(client, buf, len) < 0)
            return;
        sent += len;
    }
}

/** @brief Send one datagram back to its sender. */
static void ServeUdpEcho(int sock, const uint8_t *buf, int len, uint32_t ip, uint16_t port)
{
    SockSendTo(sock, ip, port, buf, (uint16_t)len);
}

/** @brief Count one udp-rx datagram, answer and reset on "done"; true on "quit". */
static bool ServeUdpRx(int sock, UdpRxState *st, const uint8_t *buf, int len, uint32_t ip,
                       uint16_t port)
{
    if (len == 4 && memcmp(buf, "quit", 4) == 0)
        return true;
    if (len == 4 && memcmp(buf, "done", 4) == 0) {
        uint32_t reply[2] = {st->received, st->highest};
        SockSendTo(sock, ip, port, reply, sizeof(reply));
        st->received = 0;
        st->highest = 0;
        return false;
    }
    if (len < 4)
        return false;
    uint32_t seq;
    memcpy(&seq, buf, sizeof(seq));
    st->received++;
    if (seq + 1 > st->highest)
        st->highest = seq + 1;
    return false;
}

/** @brief Open a TCP listener on @p port. */
static int OpenListener(uint16_t port)
{
    int s = SockOpen(SOCK_KIND_TCP);
    if (s < 0)
        return s;
    Err rc = SockListen(s, port, 1);
    if (rc != ZUZU_OK) {
        SockClose(s);
        return rc;
    }
    return s;
}

/** @brief Open a UDP socket bound to @p port. */
static int OpenUdp(uint16_t port)
{
    int s = SockOpen(SOCK_KIND_UDP);
    if (s < 0)
        return s;
    Err rc = SockBind(s, port);
    if (rc != ZUZU_OK) {
        SockClose(s);
        return rc;
    }
    return s;
}

/** @brief Accept one client on @p listener if one is waiting, and run @p serve on it. */
static bool AcceptAndServe(int listener, void (*serve)(int))
{
    int c = SockAccept(listener, TIMEOUT_POLL);
    if (c < 0)
        return false;
    serve(c);
    SockClose(c);
    return true;
}

int main(void)
{
    int tcp_rx = OpenListener(PORT_TCP_RX);
    int tcp_tx = OpenListener(PORT_TCP_TX);
    int udp_echo = OpenUdp(PORT_UDP_ECHO);
    int udp_rx = OpenUdp(PORT_UDP_RX);
    if (tcp_rx < 0 || tcp_tx < 0 || udp_echo < 0 || udp_rx < 0) {
        UserspaceDebugLog("netbench: socket setup failed: %d %d %d %d", tcp_rx, tcp_tx, udp_echo,
                          udp_rx);
        return ERR_BADARG;
    }
    UserspaceDebugLog("netbench: tcp %d/%d, udp %d/%d", PORT_TCP_RX, PORT_TCP_TX, PORT_UDP_ECHO,
                      PORT_UDP_RX);

    UdpRxState st = {0};
    uint8_t buf[SOCK_DGRAM_MAX];
    for (;;) {
        bool busy = AcceptAndServe(tcp_rx, ServeTcpRx);
        busy |= AcceptAndServe(tcp_tx, ServeTcpTx);

        uint32_t ip;
        uint16_t port;
        int n;
        while ((n = SockRecvFrom(udp_echo, buf, sizeof(buf), &ip, &port, TIMEOUT_POLL)) >= 0) {
            ServeUdpEcho(udp_echo, buf, n, ip, port);
            busy = true;
        }
        while ((n = SockRecvFrom(udp_rx, buf, sizeof(buf), &ip, &port, TIMEOUT_POLL)) >= 0) {
            if (ServeUdpRx(udp_rx, &st, buf, n, ip, port))
                return 0;
            busy = true;
        }

        if (!busy)
            SockWait(TIMEOUT_INFINITE);
    }
}
