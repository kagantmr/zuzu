#include "phy.h"
#include "genet.h"
#include <util/log.h>

#define ANEG_POLL_MS 100
#define ANEG_TIMEOUT_MS 15000
#define ADVERTISE_1000 0

Err PhyNegotiate(void)
{
    uint16_t ctrl1000 = 0, bmsr = 0;
    Err rc = MdioRead(MII_CTRL1000, &ctrl1000);
    if (rc == ZUZU_OK)
        rc = MdioWrite(MII_ADVERTISE,
                       ADV_CSMA | ADV_10HALF | ADV_10FULL | ADV_100HALF | ADV_100FULL);
    if (rc == ZUZU_OK)
        rc = MdioWrite(MII_CTRL1000, (uint16_t)((ctrl1000 & ~(CTRL1000_HALF | CTRL1000_FULL)) |
                                                (ADVERTISE_1000 ? CTRL1000_FULL : 0)));
    if (rc == ZUZU_OK)
        rc = MdioWrite(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "starting autonegotiation failed: %s", StrToError(rc));
        return rc;
    }

    for (uint32_t ms = 0; ms < ANEG_TIMEOUT_MS; ms += ANEG_POLL_MS) {
        Sleep(ANEG_POLL_MS);
        rc = MdioRead(MII_BMSR, &bmsr);
        if (rc == ZUZU_OK)
            rc = MdioRead(MII_BMSR, &bmsr);
        if (rc != ZUZU_OK) {
            LOG_ERROR(LOG_TAG, "MDIO read failed: %s", StrToError(rc));
            return rc;
        }
        if ((bmsr & BMSR_LSTATUS) && (bmsr & BMSR_ANEGCOMPLETE)) {
            LOG_INFO(LOG_TAG, "autonegotiation done after %u ms", ms + ANEG_POLL_MS);
            return ZUZU_OK;
        }
    }
    LOG_ERROR(LOG_TAG, "autonegotiation timed out, bmsr %04x", bmsr);
    return ERR_TIMEOUT;
}

Err PhyGetSpeed(uint32_t *umac_speed, uint32_t *mbps)
{
    uint16_t id1 = 0, id2 = 0, bmsr = 0, adv = 0, lpa = 0, ctrl1000 = 0, stat1000 = 0;
    Err rc = MdioRead(MII_PHYSID1, &id1);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_PHYSID2, &id2);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_BMSR, &bmsr);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_BMSR, &bmsr);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_ADVERTISE, &adv);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_LPA, &lpa);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_CTRL1000, &ctrl1000);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_STAT1000, &stat1000);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "MDIO read failed: %s", StrToError(rc));
        return rc;
    }
    LOG_INFO(LOG_TAG, "PHY id %04x:%04x link %u aneg %u", id1, id2, !!(bmsr & BMSR_LSTATUS),
             !!(bmsr & BMSR_ANEGCOMPLETE));

    if (!(bmsr & BMSR_LSTATUS) || !(bmsr & BMSR_ANEGCOMPLETE)) {
        LOG_ERROR(LOG_TAG, "no link");
        return ERR_NOTCONN;
    }

    LOG_INFO(LOG_TAG, "adv %04x lpa %04x ctrl1000 %04x stat1000 %04x", adv, lpa, ctrl1000,
             stat1000);
    uint16_t common = adv & lpa;
    if ((ctrl1000 & CTRL1000_FULL) && (stat1000 & STAT1000_FULL)) {
        *umac_speed = UMAC_SPEED_1000;
        *mbps = 1000;
    } else if (common & (ADV_100FULL | ADV_100HALF)) {
        *umac_speed = UMAC_SPEED_100;
        *mbps = 100;
    } else {
        *umac_speed = UMAC_SPEED_10;
        *mbps = 10;
    }
    return ZUZU_OK;
}
