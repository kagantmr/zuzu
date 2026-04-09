#include "ff.h"
#include "diskio.h"

#include "zuzu.h"
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/protocols/zusd_protocol.h"
#include <service.h>
#include <mem.h>
#include <stdint.h>

#define FAT32D_DRIVE 0
#define FAT32D_SECTOR_SIZE 512u
#define FAT32D_SECTOR_COUNT 131072u

static int g_init = 0;
static int32_t g_zusd_port = -1;
static BYTE *g_sector_buf = NULL;

static int disk_backend_init(void)
{
    if (g_init) {
        return 0;
    }

    g_zusd_port = lookup_service("zusd");
    if (g_zusd_port < 0) {
        return -1;
    }

    zuzu_ipcmsg_t r = _call(g_zusd_port, ZUSD_CMD_GET_BUF, 0, 0);
    if ((int32_t)r.r1 != 0) {
        g_zusd_port = -1;
        return -1;
    }

    g_sector_buf = (BYTE *)_attach((int32_t)r.r2);
    if ((intptr_t)g_sector_buf <= 0) {
        g_zusd_port = -1;
        g_sector_buf = NULL;
        return -1;
    }

    g_init = 1;
    return 0;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != FAT32D_DRIVE) {
        return STA_NOINIT;
    }
    return g_init ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != FAT32D_DRIVE) {
        return STA_NOINIT;
    }
    return (disk_backend_init() == 0) ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != FAT32D_DRIVE || buff == NULL || count == 0) {
        return RES_PARERR;
    }
    if (disk_backend_init() != 0) {
        return RES_NOTRDY;
    }

    for (UINT i = 0; i < count; i++) {
        zuzu_ipcmsg_t r = _call(g_zusd_port, ZUSD_CMD_READ, (uint32_t)(sector + i), 0);
        if ((int32_t)r.r1 != 0) {
            return RES_ERROR;
        }
        memcpy(buff + (i * FAT32D_SECTOR_SIZE), g_sector_buf, FAT32D_SECTOR_SIZE);
    }

    return RES_OK;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != FAT32D_DRIVE || buff == NULL || count == 0) {
        return RES_PARERR;
    }
    if (disk_backend_init() != 0) {
        return RES_NOTRDY;
    }

    for (UINT i = 0; i < count; i++) {
        memcpy(g_sector_buf, buff + (i * FAT32D_SECTOR_SIZE), FAT32D_SECTOR_SIZE);
        zuzu_ipcmsg_t r = _call(g_zusd_port, ZUSD_CMD_WRITE, (uint32_t)(sector + i), 0);
        if ((int32_t)r.r1 != 0) {
            return RES_ERROR;
        }
    }

    return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != FAT32D_DRIVE) {
        return RES_PARERR;
    }
    if (disk_backend_init() != 0) {
        return RES_NOTRDY;
    }

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;

    case GET_SECTOR_COUNT:
        if (buff == NULL) {
            return RES_PARERR;
        }
        *(LBA_t *)buff = (LBA_t)FAT32D_SECTOR_COUNT;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    return ((DWORD)(2025 - 1980) << 25)
         | ((DWORD)1 << 21)
         | ((DWORD)1 << 16);
}
