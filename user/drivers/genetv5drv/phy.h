#ifndef USER_DRIVERS_GENETV5DRV_PHY_H
#define USER_DRIVERS_GENETV5DRV_PHY_H

#define MII_BMCR 0
#define MII_BMSR 1
#define MII_PHYSID1 2
#define MII_PHYSID2 3
#define MII_ADVERTISE 4
#define MII_LPA 5
#define MII_CTRL1000 9
#define MII_STAT1000 10

#define BMCR_ANRESTART (1u << 9)
#define BMCR_ANENABLE (1u << 12)

#define BMSR_LSTATUS (1u << 2)
#define BMSR_ANEGCOMPLETE (1u << 5)
#define ADV_CSMA 0x0001u
#define ADV_10HALF (1u << 5)
#define ADV_10FULL (1u << 6)
#define ADV_100HALF (1u << 7)
#define ADV_100FULL (1u << 8)
#define CTRL1000_HALF (1u << 8)
#define CTRL1000_FULL (1u << 9)
#define STAT1000_FULL (1u << 11)

#define MII_ESTATUS 15
#define BMSR_ESTATEN (1u << 8)
#define ESTATUS_1000_THALF (1u << 12)
#define ESTATUS_1000_TFULL (1u << 13)

#endif /* USER_DRIVERS_GENETV5DRV_PHY_H */
