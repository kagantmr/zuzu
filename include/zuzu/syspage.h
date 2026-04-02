#ifndef SYSPAGE_H
#define SYSPAGE_H

#include "stdint.h"

#define SYSPAGE_MAX_DEVICES 64
#define SYSPAGE_DEV_NAME_LEN 32
#define SYSPAGE_VA 0x1000

#define SYSPAGE (void *)SYSPAGE_VA

typedef struct {
    char name[SYSPAGE_DEV_NAME_LEN]; /* "PL111 CLCD", "SMSC LAN9118", etc */
} syspage_dev_t;

typedef struct {
    uint32_t     magic;                          // Must be 0x50050CA7 "zoozoo cat"
    char         version[24];                    // version string, constant after boot
    char         build[24];                      // build timestamp, constant after boot
    char         machine[20];                    // from DTB, constant after boot
    char         cpu[24];                        // from DTB, constant after boot
    uint32_t     mem_total_kb;
    uint32_t     mem_free_kb;                    // updated 
    uint32_t     uptime_s;                       // updated by tick handler 
    uint8_t      dev_count;
    syspage_dev_t devs[SYSPAGE_MAX_DEVICES];     // filled from DTB walk at boot 
} zuzu_syspage_t;

#endif