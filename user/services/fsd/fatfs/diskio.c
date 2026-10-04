#include "ff.h"
#include "diskio.h"

#include <zuzu/zuzu.h>
#include "dev/protocols/mmcdrv.h"
#include <util/msg.h>
#include <zuzu/service.h>
#include <string.h>
#include <stdint.h>

#define FAT32D_DRIVE 0
#define FAT32D_SECTOR_SIZE 512u
#define FAT32D_SECTOR_COUNT 131072u

static int g_init = 0;
static Handle g_sd_port = -1;
static BYTE *g_buf = NULL;
static uint32_t g_buf_size = 0;
static uint32_t g_block_size = FAT32D_SECTOR_SIZE;
static uint32_t g_block_count = 0;

/* The block driver and fsd start in the same boot batch, so the first mount
 * can race the driver's registration. Retry the lookup instead of failing
 * mount() on a boot-order race. */
#define MMC_LOOKUP_RETRIES 200
#define MMC_LOOKUP_RETRY_MS 10

static Err SdCall(uint32_t cmd, uint32_t lba, uint32_t count, SdReply *rep, Handle *granted)
{
    SdRequest req = { .cmd = cmd, .lba = lba, .count = count };
    memcpy(MessageBox(), &req, sizeof(req));
    SvcResult r = Call(g_sd_port, sizeof(req), -1);
    if (granted)
        *granted = (Handle)r.r3;
    if (r.r0 != ZUZU_OK)
        return (Err)r.r0;
    if ((uint32_t)r.r1 < sizeof(Err))
        return ERR_MALFORMED;
    memset(rep, 0, sizeof(*rep));
    memcpy(rep, MessageBox(), (uint32_t)r.r1 < sizeof(*rep) ? (uint32_t)r.r1 : sizeof(*rep));
    return rep->status;
}

static int disk_backend_init(void)
{
    if (g_init) {
        return 0;
    }

    Handle port = -1;
    for (int tries = 0; tries < MMC_LOOKUP_RETRIES; tries++) {
        port = LookupService("/dev/mmc0");
        if (port >= 0) {
            break;
        }
        Sleep(MMC_LOOKUP_RETRY_MS);
    }
    if (port < 0) {
        return -1;
    }
    g_sd_port = port;

    SdReply rep;
    Handle mem = -1;
    Err rc = SdCall(SD_CMD_GET_BUF, 0, 0, &rep, &mem);
    if (rc != ZUZU_OK || mem < 0) {
        if (mem >= 0) {
            HandleClose(mem);
        }
        HandleClose(g_sd_port);
        g_sd_port = -1;
        return -1;
    }

    void * va = MemMap(mem, 0, PROT_RW);
    if (PtrIsErr(va)) {
        HandleClose(mem);
        HandleClose(g_sd_port);
        g_sd_port = -1;
        return -1;
    }

    g_buf = (BYTE *)va;
    g_buf_size = rep.buf_size;
    if (rep.block_size != 0) {
        g_block_size = rep.block_size;
    }
    g_block_count = rep.block_count;
    if (g_block_size != FAT32D_SECTOR_SIZE || g_buf_size < g_block_size) {
        MemUnmap(va);
        HandleClose(mem);
        HandleClose(g_sd_port);
        g_sd_port = -1;
        g_buf = NULL;
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

    uint32_t per = g_buf_size / FAT32D_SECTOR_SIZE;
    for (UINT done = 0; done < count;) {
        uint32_t n = count - done < per ? count - done : per;
        SdReply rep;
        if (SdCall(SD_CMD_READ, (uint32_t)(sector + done), n, &rep, NULL) != ZUZU_OK) {
            return RES_ERROR;
        }
        memcpy(buff + (done * FAT32D_SECTOR_SIZE), g_buf, n * FAT32D_SECTOR_SIZE);
        done += n;
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

    uint32_t per = g_buf_size / FAT32D_SECTOR_SIZE;
    for (UINT done = 0; done < count;) {
        uint32_t n = count - done < per ? count - done : per;
        SdReply rep;
        memcpy(g_buf, buff + (done * FAT32D_SECTOR_SIZE), n * FAT32D_SECTOR_SIZE);
        if (SdCall(SD_CMD_WRITE, (uint32_t)(sector + done), n, &rep, NULL) != ZUZU_OK) {
            return RES_ERROR;
        }
        done += n;
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
        *(LBA_t *)buff = (LBA_t)(g_block_count ? g_block_count : FAT32D_SECTOR_COUNT);
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
