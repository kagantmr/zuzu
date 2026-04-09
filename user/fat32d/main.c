#include "zuzu.h"
#include "zuzu/protocols/nt_protocol.h"
#include "zuzu/protocols/fat32d_protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <mem.h>
#include "ff.h"
#include <service.h>

static FATFS fs;
static shmem_result_t shm;
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
        _reply(reply_h, (uint32_t)FAT32_ERR_MAXFD, 0, 0);
        return;
    }

    /* arg carries FA_* mode bits directly */
    FRESULT fr = f_open(&fil_table[fd], buf, (BYTE)arg);
    if (fr != FR_OK) {
        fd_free(fd);
        int err = (fr == FR_NO_FILE || fr == FR_NO_PATH)
                  ? FAT32_ERR_NOENT : FAT32_ERR_IO;
        _reply(reply_h, (uint32_t)err, 0, 0);
        return;
    }

    _reply(reply_h, FAT32_OK, (uint32_t)fd, 0);
}

static void handle_read(uint32_t reply_h, uint32_t arg)
{
    uint32_t fd    = FAT32_RW_FD(arg);
    uint32_t count = FAT32_RW_COUNT(arg);

    if (fd >= MAX_FDS || !fil_used[fd]) {
        _reply(reply_h, (uint32_t)FAT32_ERR_BADFD, 0, 0);
        return;
    }
    if (count > 4096) count = 4096;

    UINT br = 0;
    FRESULT fr = f_read(&fil_table[fd], buf, count, &br);
    if (fr != FR_OK) {
        _reply(reply_h, (uint32_t)FAT32_ERR_IO, 0, 0);
        return;
    }

    _reply(reply_h, FAT32_OK, br, 0);
}

static void handle_write(uint32_t reply_h, uint32_t arg)
{
    uint32_t fd    = FAT32_RW_FD(arg);
    uint32_t count = FAT32_RW_COUNT(arg);

    if (fd >= MAX_FDS || !fil_used[fd]) {
        _reply(reply_h, (uint32_t)FAT32_ERR_BADFD, 0, 0);
        return;
    }
    if (count > 4096) count = 4096;

    UINT bw = 0;
    FRESULT fr = f_write(&fil_table[fd], buf, count, &bw);
    if (fr != FR_OK) {
        _reply(reply_h, (uint32_t)FAT32_ERR_IO, 0, 0);
        return;
    }

    _reply(reply_h, FAT32_OK, bw, 0);
}

static void handle_close(uint32_t reply_h, uint32_t arg)
{
    uint32_t fd = arg;
    if (fd >= MAX_FDS || !fil_used[fd]) {
        _reply(reply_h, (uint32_t)FAT32_ERR_BADFD, 0, 0);
        return;
    }

    f_close(&fil_table[fd]);
    fd_free((int)fd);
    _reply(reply_h, FAT32_OK, 0, 0);
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
        int err = (fr == FR_NO_PATH) ? FAT32_ERR_NOENT : FAT32_ERR_IO;
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
    _reply(reply_h, FAT32_OK, count, 0);
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
                  ? FAT32_ERR_NOENT : FAT32_ERR_IO;
        _reply(reply_h, (uint32_t)err, 0, 0);
        return;
    }

    fat32_stat_t *st = (fat32_stat_t *)buf;
    st->size   = (uint32_t)fno.fsize;
    st->is_dir = (fno.fattrib & AM_DIR) ? 1 : 0;
    memset(st->_pad, 0, sizeof(st->_pad));
    _reply(reply_h, FAT32_OK, 0, 0);
}

static void handle_get_buf(uint32_t reply_h, uint32_t sender)
{
    int32_t slot = _port_grant(shm.handle, (int32_t)sender);
    if (slot < 0)
        _reply(reply_h, (uint32_t)-1, 0, 0);
    else
        _reply(reply_h, 0, (uint32_t)slot, 0);
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
        _reply(reply_h, (uint32_t)-1, 0, 0);
        break;
    }
}

int main(void)
{
    zuzu_ipcmsg_t r;

    /* ask which den we're in */
    r = _call(NT_PORT, DEN_MYDEN, 0, 0);
    uint32_t my_den = 0;
    if (r.r1 != DEN_OK) {
        printf("fat32d: not in any den, using global namespace\n");
    } else {
        my_den = r.r2;
    }

    /* look up zusd (should be in same den) */
    int32_t zusd_port = lookup_service("zusd");
    if (zusd_port < 0) {
        printf("fat32d: cannot see zusd\n");
        return 1;
    }

    /* register ourselves */
    int32_t my_port = _port_create();
    int32_t nt_slot = _port_grant(my_port, NAMETABLE_PID);
    _send(NT_PORT, NT_REGISTER | (my_den << 8), nt_pack("fat3"), (uint32_t)nt_slot);

    /* mount the filesystem */
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        printf("fat32d: f_mount failed: %d\n", fr);
        return 1;
    }

    /* allocate shmem for client data transfer */
    shm = _memshare(4096);
    if (shm.handle < 0 || shm.addr == NULL) {
        printf("fat32d: shmem alloc failed\n");
        return 1;
    }
    buf = (char *)shm.addr;

    printf("fat32d: ready\n");

    while (1) {
        zuzu_ipcmsg_t msg = _recv(my_port);
        uint32_t reply_h = (uint32_t)msg.r0;
        uint32_t sender  = msg.r1;
        uint32_t cmd     = msg.r2;
        uint32_t arg     = msg.r3;
        handle_request(reply_h, sender, cmd, arg);
    }
}
