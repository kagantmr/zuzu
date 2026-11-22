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
#define ADV_PAUSE_CAP (1u << 10)
#define ADV_PAUSE_ASYM (1u << 11)
#define STAT1000_FULL (1u << 11)
#define MII_ESTATUS 15
#define MII_BCM_AUX_CTL 0x18
#define MII_BCM_SHADOW 0x1c
#define BCM_AUXCTL_SHDWSEL_MISC 0x7
#define BCM_AUXCTL_READ_SHIFT 12
#define BCM_SHADOW_CLK_CTRL 0x3
#define BCM_SHADOW_SELECT_SHIFT 10
#define BMSR_ESTATEN (1u << 8)
#define ESTATUS_1000_THALF (1u << 12)
#define ESTATUS_1000_TFULL (1u << 13)

#endif /* USER_DRIVERS_GENETV5DRV_PHY_H */
