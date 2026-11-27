#include <net/protocols/netd.h>
#include <stdio.h>
#include <string.h>
#include <zuzu/msg.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>

/** @brief Fetch the first interface at slot @p index or later into @p nif; ERR_NOENT past the end. */
static Err GetNetif(Handle netd, uint8_t index, NetdNetif *nif)
{
    NetdGetNetif req = {.op = NETD_GET_NETIF, .index = index};
    MsgWrite(&req, sizeof(req));
    uint32_t got = 0;
    Err rc = CallPort(netd, sizeof(req), -1, &got, NULL);
    if (rc != ZUZU_OK)
        return rc;
    if (got < sizeof(*nif)) {
        if (got < sizeof(rc))
            return ERR_MALFORMED;
        memcpy(&rc, GetMessageBox(), sizeof(rc));
        return rc != ZUZU_OK ? rc : ERR_MALFORMED;
    }
    memcpy(nif, GetMessageBox(), sizeof(*nif));
    return ZUZU_OK;
}

/** @brief Walk the interfaces until one is named @p name; ERR_NOENT if none is. */
static Err FindNetif(Handle netd, const char *name, NetdNetif *nif)
{
    Err rc;
    for (unsigned i = 0; (rc = GetNetif(netd, (uint8_t)i, nif)) == ZUZU_OK; i = nif->index + 1)
        if (!strncmp(nif->name, name, sizeof(nif->name)))
            return ZUZU_OK;
    return rc;
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
    NetdRxFilter req = {.op = NETD_SET_RXFILTER,
                        .index = (uint8_t)nif->index,
                        .flags = nif->rx_flags,
                        .mcast_count = nif->mcast_count};
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
            printf("usage: ifconfig [ifname] [promisc|allmulti on|off] [mcast [mac ...]]\n");
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

/** @brief Print one interface's configuration and counters. */
static void PrintNetif(const NetdNetif *n)
{
    printf("%s\n", n->name);
    printf("  MAC      %02x:%02x:%02x:%02x:%02x:%02x\n", n->mac[0], n->mac[1], n->mac[2],
           n->mac[3], n->mac[4], n->mac[5]);
    printf("  IP       %u.%u.%u.%u\n", IP4(n->ip));
    printf("  NETMASK  %u.%u.%u.%u\n", IP4(n->netmask));
    printf("  GATEWAY  %u.%u.%u.%u\n", IP4(n->gateway));
    printf("  DNS      %u.%u.%u.%u\n", IP4(n->dns));
    printf("  LINK     %s\n", n->link_up ? "up" : "down");
    printf("  FILTER   %s%s%u multicast\n", n->rx_flags & NIC_CNTL_PROMISC ? "promisc " : "",
           n->rx_flags & NIC_CNTL_ALLMULTI ? "allmulti " : "", n->mcast_count);
    printf("  RX       %u packets, %u errors, %u oversize, %u ring full, %u overflows\n",
           n->stat[NIC_STAT_RX_PACKETS], n->stat[NIC_STAT_RX_ERRORS],
           n->stat[NIC_STAT_RX_OVERSIZE], n->stat[NIC_STAT_RX_RING_FULL],
           n->stat[NIC_STAT_RX_OVERFLOWS]);
    printf("  TX       %u packets, %u drops, %u timeouts, %u underruns\n",
           n->stat[NIC_STAT_TX_PACKETS], n->stat[NIC_STAT_TX_DROPS],
           n->stat[NIC_STAT_TX_TIMEOUTS], n->stat[NIC_STAT_TX_UNDERRUNS]);
    printf("  IRQ      %u, %u unhandled\n", n->stat[NIC_STAT_IRQ], n->stat[NIC_STAT_IRQ_OTHER]);
    printf("  MAC RX   %u bytes, %u bad FCS, %u misaligned, %u runts, %u oversize\n",
           n->stat[NIC_STAT_MAC_RX_BYTES], n->stat[NIC_STAT_MAC_RX_FCS],
           n->stat[NIC_STAT_MAC_RX_ALIGN], n->stat[NIC_STAT_MAC_RX_RUNT],
           n->stat[NIC_STAT_MAC_RX_OVERSIZE]);
    printf("  MAC TX   %u bytes\n", n->stat[NIC_STAT_MAC_TX_BYTES]);
}

int main(int argc, char **argv)
{
    Handle netd = LookupService("/svc/netd");
    if (netd < 0) {
        printf("ifconfig: can't find /svc/netd (%d)\n", netd);
        return ERR_SYSDOWN;
    }

    NetdNetif nif;
    Err rc;
    if (argc == 1) {
        for (unsigned i = 0; (rc = GetNetif(netd, (uint8_t)i, &nif)) == ZUZU_OK; i = nif.index + 1)
            PrintNetif(&nif);
        if (rc != ERR_NOENT) {
            printf("ifconfig: netd call failed (%d)\n", rc);
            return rc;
        }
        return 0;
    }

    rc = FindNetif(netd, argv[1], &nif);
    if (rc == ZUZU_OK) {
        argc--;
        argv++;
    } else if (rc == ERR_NOENT && strcmp(argv[1], "promisc") && strcmp(argv[1], "allmulti") &&
               strcmp(argv[1], "mcast")) {
        printf("ifconfig: no interface named %s\n", argv[1]);
        return ERR_NOENT;
    } else {
        rc = GetNetif(netd, 0, &nif);
    }
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
        rc = GetNetif(netd, (uint8_t)nif.index, &nif);
        if (rc != ZUZU_OK)
            return rc;
    }
    PrintNetif(&nif);
    return 0;
}
