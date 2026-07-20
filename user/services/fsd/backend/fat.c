#include "../fatfs/ff.h"
#include "backend.h"
#include <string.h>

static err_t fres_to_err(FRESULT fr)
{
    switch (fr) {
    case FR_OK:                    return ZUZU_OK;
    case FR_NO_FILE:
    case FR_NO_PATH:               return ERR_NOENT;
    case FR_EXIST:                 return ERR_DUPLICATE;
    case FR_DENIED:
    case FR_WRITE_PROTECTED:       return ERR_NOPERM;
    case FR_INVALID_NAME:
    case FR_INVALID_PARAMETER:
    case FR_INVALID_OBJECT:        return ERR_MALFORMED;
    case FR_TOO_MANY_OPEN_FILES:   return ERR_BUFFULL;
    case FR_NOT_ENOUGH_CORE:       return ERR_NOMEM;
    case FR_DISK_ERR:
    case FR_INT_ERR:
    case FR_NOT_READY:
    default:                       return ERR_IO;
    }
}

static FATFS fs;

static err_t fat_open(void *ctx, void *file, const char *path, uint32_t mode)
{
    (void)ctx;
    return fres_to_err(f_open((FIL *)file, path, (BYTE)mode));
}

static err_t fat_close(void *ctx, void *file) {
    (void)ctx;
    return fres_to_err(f_close((FIL *)file));
}

static err_t fat_read(void *ctx, void *file, void *buf, uint32_t count, uint32_t *got) {
    (void)ctx;
    UINT br = 0;
    FRESULT rc = f_read((FIL *)file, buf, count, &br);
    *got = br;
    if (rc != FR_OK && *got == 0) return fres_to_err(rc);
    return ZUZU_OK;
}

static err_t fat_write(void *ctx, void *file, const void *buf, uint32_t count, uint32_t *put) {
    
    (void)ctx;
    UINT bw = 0;
    FRESULT rc = f_write((FIL *)file, buf, count, &bw);
    *put = bw;
    if (rc != FR_OK && *put == 0) return fres_to_err(rc);
    return ZUZU_OK;
}

static err_t fat_seek(void *ctx, void *file, int64_t off, uint32_t whence, int64_t *newpos) {
    (void)ctx;
    switch (whence) {
    case FSD_SEEK_SET:
        break;
    case FSD_SEEK_CUR:
        off += f_tell((FIL *)file);
        break;
    case FSD_SEEK_END:
        off += f_size((FIL *)file);
        break;
    default:
        return ERR_MALFORMED;
    }
    if (off < 0 || off > 0xFFFFFFFFLL) return ERR_MALFORMED;

    FRESULT rc = f_lseek((FIL *)file, (FSIZE_t)off);
    if (rc != FR_OK) return fres_to_err(rc);
    *newpos = (int64_t)f_tell((FIL *)file);
    return ZUZU_OK;
}

static err_t fat_stat(void *ctx, const char *path, fsd_stat_t *out) {
    (void)ctx;
    FILINFO fno;
    FRESULT rc = f_stat(path, &fno);
    if (rc != FR_OK) return fres_to_err(rc);
    out->size = (uint32_t)fno.fsize;
    out->type = (fno.fattrib & AM_DIR) ? FSD_TYPE_DIR : FSD_TYPE_FILE;
    return ZUZU_OK;
}

static err_t fat_readdir(void *ctx, const char *path, uint32_t start,
                         fsd_dirent_t *out, uint32_t max, uint32_t *count)
{
    (void)ctx;
    *count = 0;

    DIR dir;
    FRESULT rc = f_opendir(&dir, path);
    if (rc != FR_OK) return fres_to_err(rc);

    FILINFO fno;

    /* phase 1: skip `start` entries */
    for (uint32_t i = 0; i < start; i++) {
        rc = f_readdir(&dir, &fno);
        if (rc != FR_OK) { f_closedir(&dir); return fres_to_err(rc); }
        if (fno.fname[0] == '\0') { f_closedir(&dir); return ZUZU_OK; }  /* past the end */
    }

    /* phase 2: fill up to `max` */
    while (*count < max) {
        rc = f_readdir(&dir, &fno);
        if (rc != FR_OK) { f_closedir(&dir); return fres_to_err(rc); }
        if (fno.fname[0] == '\0') break;   /* end of directory */

        fsd_dirent_t *e = &out[*count];
        memset(e, 0, sizeof(*e));
        strncpy(e->name, fno.fname, sizeof(e->name) - 1);
        e->size = (uint32_t)fno.fsize;
        e->type = (fno.fattrib & AM_DIR) ? FSD_TYPE_DIR : FSD_TYPE_FILE;
        (*count)++;
    }

    f_closedir(&dir);
    return ZUZU_OK;
}

static err_t fat_unlink(void *ctx, const char *path) {
    (void)ctx;
    FRESULT rc = f_unlink(path);
    return fres_to_err(rc);
}

static err_t fat_rename(void *ctx, const char *from, const char *to) {
    (void)ctx;
    FRESULT rc = f_rename(from, to);
    return fres_to_err(rc);
}

static err_t fat_mount(void **ctx_out) {
    FRESULT rc = f_mount(&fs, "", 1);
    *ctx_out = &fs;
    return fres_to_err(rc);
}
static err_t fat_unmount(void *ctx) {
    (void)ctx;
    return fres_to_err(f_unmount(""));
}

const fs_backend_t fat_backend = {
    .name = "fatfs",
    .open = fat_open,
    .close = fat_close,
    .read = fat_read,
    .write = fat_write,
    .seek = fat_seek,
    .stat = fat_stat,
    .readdir = fat_readdir,
    .unlink = fat_unlink,
    .rename = fat_rename,
    .mount = fat_mount,
    .unmount = fat_unmount,
    .file_size = sizeof(FIL),
};

_Static_assert(sizeof(FIL) <= MAX_BACKEND_FILE_SIZE, "FIL size exceeds MAX_BACKEND_FILE_SIZE");