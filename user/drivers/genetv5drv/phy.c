#include "phy.h"
#include "genet.h"
#include <util/log.h>

Err PhyStartAutoNeg(void)
{
    uint16_t ctrl1000 = 0, bmsr = 0, estatus = 0;
    Err rc = MdioRead(MII_BMSR, &bmsr);
    if (rc == ZUZU_OK && (bmsr & BMSR_ESTATEN))
        rc = MdioRead(MII_ESTATUS, &estatus);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_CTRL1000, &ctrl1000);

    uint16_t adv1000 = 0;
    if (estatus & ESTATUS_1000_TFULL)
        adv1000 |= CTRL1000_FULL;
    if (estatus & ESTATUS_1000_THALF)
        adv1000 |= CTRL1000_HALF;
    LOG_DEBUG(LOG_TAG, "PHY 1000BASE-T: full %d half %d", !!(adv1000 & CTRL1000_FULL),
              !!(adv1000 & CTRL1000_HALF));

    if (rc == ZUZU_OK)
        rc = MdioWrite(MII_ADVERTISE,
                       ADV_CSMA | ADV_10HALF | ADV_10FULL | ADV_100HALF | ADV_100FULL);
    if (rc == ZUZU_OK)
        rc = MdioWrite(MII_CTRL1000,
                       (uint16_t)((ctrl1000 & ~(CTRL1000_HALF | CTRL1000_FULL)) | adv1000));
    if (rc == ZUZU_OK)
        rc = MdioWrite(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
    if (rc != ZUZU_OK) {
        LOG_ERROR(LOG_TAG, "starting autonegotiation failed: %s", StrToError(rc));
        return rc;
    }

    return rc;
}

Err PhyGetSpeed(uint32_t *umac_speed, uint32_t *mbps)
{
    uint16_t id1 = 0, id2 = 0, bmsr = 0, adv = 0, lpa = 0, ctrl1000 = 0, stat1000 = 0;
    Err rc = MdioRead(MII_PHYSID1, &id1);
    if (rc == ZUZU_OK)
        rc = MdioRead(MII_PHYSID2, &id2);
    if (rc == ZUZU_OK)
        rc = MdioReadLatched(MII_BMSR, &bmsr);
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
    if (!(bmsr & BMSR_LSTATUS) || !(bmsr & BMSR_ANEGCOMPLETE)) {
        LOG_ERROR(LOG_TAG, "no link");
        return ERR_NOTCONN;
    }

    LOG_DEBUG(LOG_TAG, "PHY id %04x:%04x adv %04x lpa %04x ctrl1000 %04x stat1000 %04x", id1, id2,
              adv, lpa, ctrl1000, stat1000);
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

void PhyPollLink(void)
{
    uint16_t bmsr = 0;
    if (MdioReadLatched(MII_BMSR, &bmsr) != ZUZU_OK)
        return;
    bool up = (bmsr & BMSR_LSTATUS) && (bmsr & BMSR_ANEGCOMPLETE);
    if (up == g_genet.link_up)
        return;

    if (!up) {
        g_genet.link_up = false;
        LOG_INFO(LOG_TAG, "link down");
        return;
    }

    uint32_t umac_speed = 0, mbps = 0;
    if (PhyGetSpeed(&umac_speed, &mbps) != ZUZU_OK)
        return;
    SetLink(umac_speed);
    g_genet.mbps = mbps;
    g_genet.link_up = true;
    LOG_INFO(LOG_TAG, "link up %u Mb/s", mbps);
}
