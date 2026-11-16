#include "zzsh.h"
#include <ansi.h>
#include <fs/fsd_client.h>
#include <net/socket.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/err.h>
#include <zuzu/uptime.h>
#include <zuzu/zuzu.h>

#define TFTP_PORT 69
#define TFTP_BLOCK 512
#define TFTP_TIMEOUT_MS 1000
#define TFTP_RETRIES 5

#define OP_RRQ 1
#define OP_DATA 3
#define OP_ACK 4
#define OP_ERROR 5

static bool parse_ip(const char *s, uint32_t *out)
{
    uint32_t ip = 0;
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9')
            return false;
        uint32_t v = 0;
        while (*s >= '0' && *s <= '9')
            v = v * 10 + (uint32_t)(*s++ - '0');
        if (v > 255 || (i < 3 && *s++ != '.'))
            return false;
        ip |= v << (8 * i);
    }
    if (*s)
        return false;
    *out = ip;
    return true;
}

static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void send_ack(int sock, uint32_t ip, uint16_t port, uint16_t block)
{
    uint8_t ack[4];
    put16(ack, OP_ACK);
    put16(ack + 2, block);
    SockSendTo(sock, ip, port, ack, sizeof(ack));
}

static int tftp_fetch(FsdConn *fsd, int sock, uint32_t server, const char *file, const char *dest)
{
    uint8_t rrq[SOCK_DGRAM_MAX];
    size_t flen = strlen(file);
    if (flen + 9 > sizeof(rrq)) {
        printf("tftp: name too long\n");
        return -1;
    }
    put16(rrq, OP_RRQ);
    memcpy(rrq + 2, file, flen + 1);
    memcpy(rrq + 3 + flen, "octet", 6);
    uint16_t rrq_len = (uint16_t)(flen + 9);

    uint32_t fd;
    bool open = false;
    Err rc;
    uint8_t pkt[4 + TFTP_BLOCK];
    uint16_t tid = 0;
    uint16_t expect = 1;
    uint32_t total = 0;
    int tries = 0;
    int result = -1;
    uint64_t start = UptimeMs();

    SockSendTo(sock, server, TFTP_PORT, rrq, rrq_len);
    for (;;) {
        uint32_t ip;
        uint16_t port;
        int n = SockRecvFrom(sock, pkt, sizeof(pkt), &ip, &port, TFTP_TIMEOUT_MS);
        if (n == ERR_TIMEOUT) {
            if (++tries > TFTP_RETRIES) {
                printf(ANSI_RED "tftp: timed out\n" ANSI_RESET);
                break;
            }
            if (expect == 1)
                SockSendTo(sock, server, TFTP_PORT, rrq, rrq_len);
            else
                send_ack(sock, server, tid, (uint16_t)(expect - 1));
            continue;
        }
        if (n < 0) {
            printf(ANSI_RED "tftp: recv failed (%s)\n" ANSI_RESET, StrToError(n));
            break;
        }
        if (ip != server || n < 4 || (tid && port != tid))
            continue;

        uint16_t op = get16(pkt);
        if (op == OP_ERROR) {
            pkt[n < (int)sizeof(pkt) ? n : (int)sizeof(pkt) - 1] = 0;
            printf(ANSI_RED "tftp: server: %s\n" ANSI_RESET, (char *)pkt + 4);
            break;
        }
        if (op != OP_DATA)
            continue;
        if (!tid)
            tid = port;

        uint16_t block = get16(pkt + 2);
        if (block != expect) {
            if (block == (uint16_t)(expect - 1))
                send_ack(sock, server, tid, block);
            continue;
        }

        if (!open) {
            rc = FsdOpen(fsd, dest, FSD_MODE_WRITE | FSD_MODE_CREATE_ALWAYS, &fd);
            if (rc != ZUZU_OK) {
                printf(ANSI_RED "tftp: %s: open failed (%s)\n" ANSI_RESET, dest, StrToError(rc));
                break;
            }
            open = true;
        }

        uint32_t len = (uint32_t)n - 4;
        if (len) {
            uint32_t put;
            rc = FsdWrite(fsd, fd, pkt + 4, len, &put);
            if (rc != ZUZU_OK || put != len) {
                printf(ANSI_RED "tftp: write failed (%s)\n" ANSI_RESET, StrToError(rc));
                break;
            }
        }
        total += len;
        tries = 0;
        send_ack(sock, server, tid, block);
        expect++;

        if (len < TFTP_BLOCK) {
            uint64_t ms = UptimeMs() - start;
            printf("%s: %u bytes in %u ms\n", dest, total, (unsigned)ms);
            result = 0;
            break;
        }
    }

    if (open) {
        FsdClose(fsd, fd);
        if (result != 0)
            FsdUnlink(fsd, dest);
    }
    return result;
}

int TftpGet(FsdConn *fsd, const char *server, const char *file, const char *dest)
{
    uint32_t ip;
    if (!parse_ip(server, &ip)) {
        printf("tftp: bad address %s\n", server);
        return -1;
    }

    int sock = SockOpen(SOCK_KIND_UDP);
    if (sock < 0) {
        printf(ANSI_RED "tftp: socket failed (%s)\n" ANSI_RESET, StrToError(sock));
        return -1;
    }
    Err rc = SockBind(sock, 0);
    if (rc != ZUZU_OK) {
        printf(ANSI_RED "tftp: bind failed (%s)\n" ANSI_RESET, StrToError(rc));
        SockClose(sock);
        return -1;
    }

    int result = tftp_fetch(fsd, sock, ip, file, dest);
    SockClose(sock);
    return result;
}
