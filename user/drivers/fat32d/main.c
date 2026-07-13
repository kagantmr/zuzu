#include <zuzu/zuzu.h>
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/protocols/fat32d_protocol.h"
#include <zuzu/log.h>
#include <stdbool.h>
#include <string.h>
#include <mem.h>
#include "ff.h"
#include <zuzu/service.h>

#define LOG_TAG "fat32d"

static FATFS fs;
static handle_t shm_handle = -1;
static char *buf;  /* pointer to shmem region */

/* ---- file descriptor table ---- */

#define MAX_FDS 16

static FIL  fil_table[MAX_FDS];
static bool fil_used[MAX_FDS];

static int fd_alloc(void)
{
    for (int i = 0; i < MAX_FDS; i++) {
        if (!fil_used[i]) {
            fil_used[i] = true;
            return i;
        }
    }
    return -1;
}

static void fd_free(int fd)
{
    if (fd >= 0 && fd < MAX_FDS)
        fil_used[fd] = false;
}

/* ---- handlers ---- */

static void handle_open(uint32_t reply_h, uint32_t arg)
{
    int fd = fd_alloc();
    if (fd < 0) {
        _reply(reply_h, (uint32_t)ERR_BUFFULL, 0, 0);
        return;
    }

    /* arg carries FA_* mode bits directly */
    FRESULT fr = f_open(&fil_table[fd], buf, (BYTE)arg);
    if (fr != FR_OK) {
        fd_free(fd);
        int err = (fr == FR_NO_FILE || fr == FR_NO_PATH)
                  ? ERR_NOENT : FAT32_ERR_IO;
        _reply(reply_h, (uint32_t)err, 0, 0);
        return;
    }

    _reply(reply_h, ZUZU_OK, (uint32_t)fd, 0);
}

static void handle_read(uint32_t reply_h, uint32_t arg)
{
    uint32_t fd    = FAT32_RW_FD(arg);
    uint32_t count = FAT32_RW_COUNT(arg);

    if (fd >= MAX_FDS || !fil_used[fd]) {
        _reply(reply_h, (uint32_t)ERR_MALFORMED, 0, 0);
        return;
    }
    if (count > 32768) count = 32768;

    UINT br = 0;
    FRESULT fr = f_read(&fil_table[fd], buf, count, &br);
    if (fr != FR_OK) {
        _reply(reply_h, (uint32_t)FAT32_ERR_IO, 0, 0);
        return;
    }

    _reply(reply_h, ZUZU_OK, br, 0);
}

static void handle_write(uint32_t reply_h, uint32_t arg)
{
    uint32_t fd    = FAT32_RW_FD(arg);
    uint32_t count = FAT32_RW_COUNT(arg);

    if (fd >= MAX_FDS || !fil_used[fd]) {
        _reply(reply_h, (uint32_t)ERR_MALFORMED, 0, 0);
        return;
    }
    if (count > 32768) count = 32768;

    UINT bw = 0;
    FRESULT fr = f_write(&fil_table[fd], buf, count, &bw);
    if (fr != FR_OK) {
        _reply(reply_h, (uint32_t)FAT32_ERR_IO, 0, 0);
        return;
    }

    _reply(reply_h, ZUZU_OK, bw, 0);
}

static void handle_close(uint32_t reply_h, uint32_t arg)
{
    uint32_t fd = arg;
    if (fd >= MAX_FDS || !fil_used[fd]) {
        _reply(reply_h, (uint32_t)ERR_MALFORMED, 0, 0);
        return;
    }

    f_close(&fil_table[fd]);
    fd_free((int)fd);
    _reply(reply_h, ZUZU_OK, 0, 0);
}

static void handle_readdir(uint32_t reply_h)
{
    /* path is in shmem — save before we overwrite buf with results */
    char path[128];
    strncpy(path, buf, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    DIR dir;
    FRESULT fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        int err = (fr == FR_NO_PATH) ? ERR_NOENT : FAT32_ERR_IO;
        _reply(reply_h, (uint32_t)err, 0, 0);
        return;
    }

    fat32_dirent_t *out = (fat32_dirent_t *)buf;
    uint32_t max_entries = 4096 / sizeof(fat32_dirent_t);
    uint32_t count = 0;

    FILINFO fno;
    while (count < max_entries) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0')
            break;

        memset(&out[count], 0, sizeof(fat32_dirent_t));
        strncpy(out[count].name, fno.fname, sizeof(out[count].name) - 1);
        out[count].size   = (uint32_t)fno.fsize;
        out[count].is_dir = (fno.fattrib & AM_DIR) ? 1 : 0;
        count++;
    }

    f_closedir(&dir);
    _reply(reply_h, ZUZU_OK, count, 0);
}

static void handle_stat(uint32_t reply_h)
{
    /* path in shmem — save before overwriting */
    char path[128];
    strncpy(path, buf, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    FILINFO fno;
    FRESULT fr = f_stat(path, &fno);
    if (fr != FR_OK) {
        int err = (fr == FR_NO_FILE || fr == FR_NO_PATH)
                  ? ERR_NOENT : FAT32_ERR_IO;
        _reply(reply_h, (uint32_t)err, 0, 0);
        return;
    }

    fat32_stat_t *st = (fat32_stat_t *)buf;
    st->size   = (uint32_t)fno.fsize;
    st->is_dir = (fno.fattrib & AM_DIR) ? 1 : 0;
    memset(st->_pad, 0, sizeof(st->_pad));
    _reply(reply_h, ZUZU_OK, 0, 0);
}

static void handle_get_buf(uint32_t reply_h, uint32_t sender)
{
    int32_t slot = _grant(shm_handle, (int32_t)sender);
    if (slot < 0)
        _reply(reply_h, (uint32_t)slot, 0, 0);
    else
        _reply(reply_h, ZUZU_OK, (uint32_t)slot, 0);
}

static void handle_request(uint32_t reply_h, uint32_t sender,
                           uint32_t cmd, uint32_t arg)
{
    switch (cmd) {
    case FAT32_OPEN:    handle_open(reply_h, arg);        break;
    case FAT32_READ:    handle_read(reply_h, arg);        break;
    case FAT32_WRITE:   handle_write(reply_h, arg);       break;
    case FAT32_CLOSE:   handle_close(reply_h, arg);       break;
    case FAT32_READDIR: handle_readdir(reply_h);          break;
    case FAT32_STAT:    handle_stat(reply_h);             break;
    case FAT32_GET_BUF: handle_get_buf(reply_h, sender);  break;
    default:
        _reply(reply_h, (uint32_t)ERR_NOMATCH, 0, 0);
        break;
    }
}

int main(void)
{
    msg_t r;

    /* ask which den we're in */
    r = _call(NT_PORT, DEN_MYDEN, 0, 0);
    uint32_t my_den = 0;
    if (r.r1 != DEN_OK) {
        LOG_WARN(LOG_TAG, "not in any den, using global namespace");
    } else {
        my_den = r.r2;
    }

    /* look up pl181drv (should be in same den) */
    int32_t sd_port = lookup_service("pl181drv");
    if (sd_port < 0) {
        LOG_ERROR(LOG_TAG, "cannot see pl181drv");
        return 1;
    }

    /* register ourselves */
    int32_t my_port = _port_create();
    int32_t nt_slot = _grant(my_port, NAMETABLE_PID);
    _send(NT_PORT, NT_REGISTER | (my_den << 8), nt_pack("fat3"), (uint32_t)nt_slot);

    /* mount the filesystem */
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        LOG_ERROR(LOG_TAG, "f_mount failed: %d", fr);
        return 1;
    }

    /* allocate shmem for client data transfer */
    shm_handle = _shm_create(32768);
    if (shm_handle < 0) {
        LOG_ERROR(LOG_TAG, "shmem alloc failed");
        return 1;
    }
    buf = (char *)_attach(shm_handle, VM_PROT_READ | VM_PROT_WRITE);
    if (_ptr_is_err(buf)) {
        LOG_ERROR(LOG_TAG, "shmem attach failed");
        return 1;
    }

    LOG_INFO(LOG_TAG, "ready");

    while (1) {
        msg_t msg = _recv(my_port);
        uint32_t reply_h = (uint32_t)msg.r0;
        uint32_t sender  = msg.r1;
        uint32_t cmd     = msg.r2;
        uint32_t arg     = msg.r3;
        handle_request(reply_h, sender, cmd, arg);
    }
}
