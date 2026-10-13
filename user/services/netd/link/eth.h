#ifndef NETD_ETHERNET_H
#define NETD_ETHERNET_H

#include "../common/globals.h"
#include "../common/txframe.h"
#include <convert.h>
#include <net/packetring.h>
#include <types.h>

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP 0x0800

/* NIC should already remove the other fields */
typedef struct __attribute__((packed)) {
    MacAddr dst_mac;
    MacAddr src_mac;
    uint16_t ethertype;
} EthHdr;

int EthRx(uint8_t *data, uint16_t len);
int EthTx(MacAddr dst_mac, uint16_t ethertype, uint8_t *payload, uint16_t len);

/* Zero-copy send: prepend the Ethernet header onto an already-built frame and
   publish its tx-ring slot. The builder's headroom must leave exactly enough
   room so the frame ends up starting at slot->data[0]. */
int EthSendFrame(TxFrame *f, MacAddr dst_mac, uint16_t ethertype);

#endif /* NETD_ETHERNET_H */