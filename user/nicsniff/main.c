#include <stdio.h>
#include <zuzu/service.h>
#include <zuzu/ipc.h>
#include <zuzu/umem.h>
#include <zuzu/types.h>
#include <zuzu/protocols/nic_protocol.h>
#include <zuzu/packetring.h>
#include <stdint.h>
#include <string.h>

#define SNIFF_COUNT 4

static void hexdump(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (i > 0 && (i % 16) == 0)
            printf("\n");
        else if (i > 0 && (i % 8) == 0)
            printf(" ");
        printf("%02x ", buf[i]);
    }
    printf("\n");
}

static void send_arp_probe(int32_t nic_port, nic_ring_t *tx_ring, const uint8_t mac[6])
{
    uint8_t buf[42];
    memset(buf, 0xff, 6);
    memcpy(buf + 6, mac, 6);
    buf[12] = 0x08; buf[13] = 0x06;
    buf[14] = 0x00; buf[15] = 0x01;
    buf[16] = 0x08; buf[17] = 0x00;
    buf[18] = 6;    buf[19] = 4;
    buf[20] = 0x00; buf[21] = 0x01;
    memcpy(buf + 22, mac, 6);
    buf[28] = 10; buf[29] = 0; buf[30] = 2; buf[31] = 15;
    memset(buf + 32, 0, 6);
    buf[38] = 10; buf[39] = 0; buf[40] = 2; buf[41] = 2;

    packet_ring_push(tx_ring, buf, 42);
    _call(nic_port, NIC_CMD_SEND, 0, 0);
}

int main(void)
{
    int32_t nic_port = lookup_service("nic0");
    if (nic_port < 0) {
        printf("nicsniff: nic0 not found\n");
        return 1;
    }

    // r2 = mac_lo on success, ERR_SYSDOWN (<0) on failure
    msg_t r = _call(nic_port, NIC_CMD_GETMAC, 0, 0);
    if ((int32_t)r.r2 < 0) {
        printf("nicsniff: GETMAC failed\n");
        return 1;
    }
    uint8_t mac[6];
    mac[0] = (r.r2 >>  0) & 0xff;
    mac[1] = (r.r2 >>  8) & 0xff;
    mac[2] = (r.r2 >> 16) & 0xff;
    mac[3] = (r.r2 >> 24) & 0xff;
    mac[4] = (r.r3 >>  0) & 0xff;
    mac[5] = (r.r3 >>  8) & 0xff;
    printf("nicsniff: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // r1 = error on failure; r3 = shmem_handle on success
    r = _call(nic_port, NIC_CMD_GETBUF, 0, 0);
    if ((int32_t)r.r1 != 0) {
        printf("nicsniff: GETBUF failed\n");
        return 1;
    }
    void *shmem_addr = _attach((int32_t)r.r3);
    if ((intptr_t)shmem_addr <= 0) {
        printf("nicsniff: shmem attach failed\n");
        return 1;
    }

    nic_ring_t *rx_ring = (nic_ring_t *)shmem_addr;
    nic_ring_t *tx_ring = (nic_ring_t *)((uint8_t *)shmem_addr + 8192);

    send_arp_probe(nic_port, tx_ring, mac);
    printf("nicsniff: sent ARP who-has 10.0.2.2, waiting for packets...\n");

    r = _call(nic_port, NIC_CMD_RECV, 0, 0);
    if ((int32_t)r.r1 != 0) {
        printf("nicsniff: RECV failed (%d)\n", (int32_t)r.r1);
        return 1;
    }
    nic_frame_t frame;
    if (packet_ring_pop(&frame, rx_ring) != 0) {
        printf("nicsniff: ring empty after RECV\n");
        return 1;
    }
    uint32_t dump_len = frame.len < 64 ? frame.len : 64;
    printf("nicsniff: len=%u\n", frame.len);
    hexdump(frame.data, dump_len);
    if (frame.len > 64)
        printf("  ... (%u more bytes)\n", frame.len - 64);

    return 0;
}
