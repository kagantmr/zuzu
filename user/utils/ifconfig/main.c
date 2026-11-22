#include <net/protocols/netd.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

/** @brief Fetch the interface description from netd into @p nif. */
static Err GetNetif(Handle netd, NetdNetif *nif)
{
    uint8_t op = NETD_GET_NETIF;
    MsgWrite(&op, sizeof(op));
    uint32_t got = 0;
    Err rc = CallPort(netd, sizeof(op), -1, &got, NULL);
    if (rc != ZUZU_OK || got < sizeof(*nif))
        return rc != ZUZU_OK ? rc : ERR_MALFORMED;
    memcpy(nif, GetMessageBox(), sizeof(*nif));
    return ZUZU_OK;
}

/** @brief Parse "aa:bb:cc:dd:ee:ff" into @p mac; false if it is not a MAC address. */
static bool ParseMac(const char *s, MacAddr mac)
{
    for (int i = 0; i < MAC_ADDR_LEN; i++) {
        unsigned v = 0;
        int digits = 0;
        for (; digits < 2; digits++, s++) {
            char c = *s;
            unsigned d = c >= '0' && c <= '9'   ? (unsigned)(c - '0')
                         : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10)
                         : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10)
                                                : 16u;
            if (d == 16u)
                break;
            v = v * 16 + d;
        }
        if (digits == 0 || *s != (i == MAC_ADDR_LEN - 1 ? '\0' : ':'))
            return false;
        mac[i] = (uint8_t)v;
        if (i < MAC_ADDR_LEN - 1)
            s++;
    }
    return true;
}

/** @brief Apply "promisc on|off", "allmulti on|off" or "mcast [mac...]" on top of the current
 * filter. */
static Err SetFilter(Handle netd, const NetdNetif *nif, int argc, char **argv)
{
    NetdRxFilter req = {
        .op = NETD_SET_RXFILTER, .flags = nif->rx_flags, .mcast_count = nif->mcast_count};
    memcpy(req.mcast, nif->mcast, sizeof(req.mcast));

    if (!strcmp(argv[1], "mcast")) {
        if (argc - 2 > NIC_MCAST_MAX) {
            printf("ifconfig: at most %d multicast addresses\n", NIC_MCAST_MAX);
            return ERR_BADARG;
        }
        req.mcast_count = 0;
        for (int i = 2; i < argc; i++) {
            if (!ParseMac(argv[i], req.mcast[req.mcast_count++]) ||
                !(req.mcast[req.mcast_count - 1][0] & 1)) {
                printf("ifconfig: '%s' is not a multicast MAC address\n", argv[i]);
                return ERR_BADARG;
            }
        }
    } else {
        uint32_t bit = !strcmp(argv[1], "promisc")    ? NIC_CNTL_PROMISC
                       : !strcmp(argv[1], "allmulti") ? NIC_CNTL_ALLMULTI
                                                      : 0;
        bool on = argc == 3 && !strcmp(argv[2], "on");
        if (!bit || argc != 3 || (!on && strcmp(argv[2], "off"))) {
            printf("usage: ifconfig [promisc|allmulti on|off] [mcast [mac ...]]\n");
            return ERR_BADARG;
        }
        req.flags = on ? (req.flags | bit) : (req.flags & ~bit);
    }

    MsgWrite(&req, sizeof(req));
    uint32_t got = 0;
    Err rc = CallPort(netd, sizeof(req), -1, &got, NULL);
    if (rc == ZUZU_OK && got >= sizeof(Err))
        memcpy(&rc, GetMessageBox(), sizeof(rc));
    return rc;
}

int main(int argc, char **argv)
{
    Handle netd = LookupService("/svc/netd");
    if (netd < 0) {
        printf("ifconfig: can't find /svc/netd (%d)\n", netd);
        return ERR_SYSDOWN;
    }

    NetdNetif nif;
    Err rc = GetNetif(netd, &nif);
    if (rc != ZUZU_OK) {
        printf("ifconfig: netd call failed (%d)\n", rc);
        return rc;
    }

    if (argc > 1) {
        rc = SetFilter(netd, &nif, argc, argv);
        if (rc != ZUZU_OK) {
            printf("ifconfig: cannot set the filter (%d)\n", rc);
            return rc;
        }
        rc = GetNetif(netd, &nif);
        if (rc != ZUZU_OK)
            return rc;
    }

    printf("%s\n", nif.name);
    printf("  MAC      %02x:%02x:%02x:%02x:%02x:%02x\n", nif.mac[0], nif.mac[1], nif.mac[2],
           nif.mac[3], nif.mac[4], nif.mac[5]);
    printf("  IP       %u.%u.%u.%u\n", IP4(nif.ip));
    printf("  NETMASK  %u.%u.%u.%u\n", IP4(nif.netmask));
    printf("  GATEWAY  %u.%u.%u.%u\n", IP4(nif.gateway));
    printf("  DNS      %u.%u.%u.%u\n", IP4(nif.dns));
    printf("  LINK     %s\n", nif.link_up ? "up" : "down");
    printf("  FILTER   %s%s%u multicast\n", nif.rx_flags & NIC_CNTL_PROMISC ? "promisc " : "",
           nif.rx_flags & NIC_CNTL_ALLMULTI ? "allmulti " : "", nif.mcast_count);
    printf("  RX       %u packets, %u errors, %u oversize, %u ring full, %u overflows\n",
           nif.stat[NIC_STAT_RX_PACKETS], nif.stat[NIC_STAT_RX_ERRORS],
           nif.stat[NIC_STAT_RX_OVERSIZE], nif.stat[NIC_STAT_RX_RING_FULL],
           nif.stat[NIC_STAT_RX_OVERFLOWS]);
    printf("  TX       %u packets, %u drops, %u timeouts, %u underruns\n",
           nif.stat[NIC_STAT_TX_PACKETS], nif.stat[NIC_STAT_TX_DROPS],
           nif.stat[NIC_STAT_TX_TIMEOUTS], nif.stat[NIC_STAT_TX_UNDERRUNS]);
    printf("  IRQ      %u, %u unhandled\n", nif.stat[NIC_STAT_IRQ], nif.stat[NIC_STAT_IRQ_OTHER]);
    printf("  MAC RX   %u bytes, %u bad FCS, %u misaligned, %u runts, %u oversize\n",
           nif.stat[NIC_STAT_MAC_RX_BYTES], nif.stat[NIC_STAT_MAC_RX_FCS],
           nif.stat[NIC_STAT_MAC_RX_ALIGN], nif.stat[NIC_STAT_MAC_RX_RUNT],
           nif.stat[NIC_STAT_MAC_RX_OVERSIZE]);
    printf("  MAC TX   %u bytes\n", nif.stat[NIC_STAT_MAC_TX_BYTES]);
    return 0;
}
