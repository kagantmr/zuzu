#include <zuzu/protocols/fat32d_protocol.h>
#include <zuzu/protocols/fbox_protocol.h>
#include <zuzu/protocols/nt_protocol.h>
#include <service.h>
#include <zuzu.h>
#include <stdio.h>
#include <string.h>
#include <mem.h>
#include <zmalloc.h>

static int32_t fat32d_port = -1;
static void *fat32d_buf = NULL;    /* shmem shared with fat32d */
static shmem_result_t my_shm;     /* shmem shared with clients */
static char *my_buf = NULL;

static int32_t spawn_from_sd(const char *path, const char *proc_name)
{
    size_t plen = strlen(path);
    if (plen == 0 || plen >= 4096)
        return -1;

    memcpy(fat32d_buf, path, plen + 1);
    zuzu_ipcmsg_t r = _call(fat32d_port, FAT32_STAT, 0, 0);
    if ((int32_t)r.r1 != FAT32_OK)
        return -1;

    fat32_stat_t *st = (fat32_stat_t *)fat32d_buf;
    uint32_t file_size = st->size;
    uint8_t is_dir = st->is_dir;
    if (is_dir || file_size == 0)
        return -1;

    memcpy(fat32d_buf, path, plen + 1);
    r = _call(fat32d_port, FAT32_OPEN, FAT32_MODE_READ, 0);
    if ((int32_t)r.r1 != FAT32_OK)
        return -1;

    uint32_t fd = r.r2;
    uint8_t *elf = (uint8_t *)zmalloc(file_size);
    if (!elf) {
        (void)_call(fat32d_port, FAT32_CLOSE, fd, 0);
        return -1;
    }

    uint32_t total = 0;
    while (total < file_size) {
        uint32_t chunk = file_size - total;
        if (chunk > 4096)
            chunk = 4096;

        r = _call(fat32d_port, FAT32_READ, FAT32_PACK_RW(fd, chunk), 0);
        if ((int32_t)r.r1 != FAT32_OK || r.r2 == 0)
            break;

        memcpy(elf + total, fat32d_buf, r.r2);
        total += r.r2;
    }

    (void)_call(fat32d_port, FAT32_CLOSE, fd, 0);
    if (total != file_size) {
        zfree(elf);
        return -1;
    }

    int32_t pid = _spawn(elf, total, proc_name, strlen(proc_name));
    zfree(elf);
    return pid;
}

/* Copy client shmem -> fat32d shmem, forward command, copy result back.
 * For commands where the client writes data IN (path, write data):
 *   copy my_buf -> fat32d_buf before the call.
 * For commands where fat32d writes data OUT (read data, dirents, stat):
 *   copy fat32d_buf -> my_buf after the call.
 */

static void proxy_open(uint32_t reply_h, uint32_t arg)
{
    /* path in my_buf -> fat32d_buf */
    size_t plen = strlen(my_buf);
    memcpy(fat32d_buf, my_buf, plen + 1);

    zuzu_ipcmsg_t r = _call(fat32d_port, FAT32_OPEN, arg, 0);
    _reply(reply_h, r.r1, r.r2, 0);
}

static void proxy_read(uint32_t reply_h, uint32_t arg)
{
    zuzu_ipcmsg_t r = _call(fat32d_port, FAT32_READ, arg, 0);

    /* data in fat32d_buf -> my_buf */
    uint32_t count = FAT32_RW_COUNT(arg);
    if (count > 4096) count = 4096;
    uint32_t got = r.r2;
    if (got > count) got = count;
    if ((int32_t)r.r1 == FAT32_OK && got > 0)
        memcpy(my_buf, fat32d_buf, got);

    _reply(reply_h, r.r1, r.r2, 0);
}

static void proxy_write(uint32_t reply_h, uint32_t arg)
{
    /* data in my_buf -> fat32d_buf */
    uint32_t count = FAT32_RW_COUNT(arg);
    if (count > 4096) count = 4096;
    memcpy(fat32d_buf, my_buf, count);

    zuzu_ipcmsg_t r = _call(fat32d_port, FAT32_WRITE, arg, 0);
    _reply(reply_h, r.r1, r.r2, 0);
}

static void proxy_close(uint32_t reply_h, uint32_t arg)
{
    zuzu_ipcmsg_t r = _call(fat32d_port, FAT32_CLOSE, arg, 0);
    _reply(reply_h, r.r1, 0, 0);
}

static void proxy_readdir(uint32_t reply_h)
{
    /* path in my_buf -> fat32d_buf */
    size_t plen = strlen(my_buf);
    memcpy(fat32d_buf, my_buf, plen + 1);

    zuzu_ipcmsg_t r = _call(fat32d_port, FAT32_READDIR, 0, 0);

    /* dirents in fat32d_buf -> my_buf */
    if ((int32_t)r.r1 == FAT32_OK && r.r2 > 0) {
        uint32_t bytes = r.r2 * sizeof(fat32_dirent_t);
        if (bytes > 4096) bytes = 4096;
        memcpy(my_buf, fat32d_buf, bytes);
    }

    _reply(reply_h, r.r1, r.r2, 0);
}

static void proxy_stat(uint32_t reply_h)
{
    /* path in my_buf -> fat32d_buf */
    size_t plen = strlen(my_buf);
    memcpy(fat32d_buf, my_buf, plen + 1);

    zuzu_ipcmsg_t r = _call(fat32d_port, FAT32_STAT, 0, 0);

    /* stat result in fat32d_buf -> my_buf */
    if ((int32_t)r.r1 == FAT32_OK)
        memcpy(my_buf, fat32d_buf, sizeof(fat32_stat_t));

    _reply(reply_h, r.r1, 0, 0);
}

static void handle_get_buf(uint32_t reply_h, uint32_t sender)
{
    int32_t slot = _port_grant(my_shm.handle, (int32_t)sender);
    if (slot < 0)
        _reply(reply_h, (uint32_t)-1, 0, 0);
    else
        _reply(reply_h, 0, (uint32_t)slot, 0);
}

static void handle_request(uint32_t reply_h, uint32_t sender,
                           uint32_t cmd, uint32_t arg)
{
    switch (cmd) {
    case FBOX_OPEN:    proxy_open(reply_h, arg);          break;
    case FBOX_READ:    proxy_read(reply_h, arg);          break;
    case FBOX_WRITE:   proxy_write(reply_h, arg);         break;
    case FBOX_CLOSE:   proxy_close(reply_h, arg);         break;
    case FBOX_READDIR: proxy_readdir(reply_h);            break;
    case FBOX_STAT:    proxy_stat(reply_h);               break;
    case FBOX_GET_BUF: handle_get_buf(reply_h, sender);   break;
    default:
        _reply(reply_h, (uint32_t)-1, 0, 0);
        break;
    }
}

int main(void)
{
    /* register in disk den */
    int32_t my_port = register_service("fbox");
    if (my_port < 0) {
        printf("fbox: register failed\n");
        return 1;
    }

    /* also register in global namespace so any client can find us */
    int32_t global_slot = _port_grant(my_port, NAMETABLE_PID);
    if (global_slot < 0) {
        printf("fbox: global grant failed\n");
        return 1;
    }
    _send(NT_PORT, NT_REGISTER | (0 << 8), nt_pack("fbox"), (uint32_t)global_slot);

    /* look up fat32d */
    fat32d_port = lookup_service("fat3");
    if (fat32d_port < 0) {
        printf("fbox: cannot find fat32d\n");
        return 1;
    }

    /* get fat32d's shmem buffer */
    zuzu_ipcmsg_t r = _call(fat32d_port, FAT32_GET_BUF, 0, 0);
    if ((int32_t)r.r1 != 0) {
        printf("fbox: FAT32_GET_BUF failed\n");
        return 1;
    }
    fat32d_buf = _attach((int32_t)r.r2);
    if ((intptr_t)fat32d_buf <= 0) {
        printf("fbox: attach failed\n");
        return 1;
    }

    /* allocate our own shmem for clients */
    my_shm = _memshare(4096);
    if (my_shm.handle < 0 || !my_shm.addr) {
        printf("fbox: shmem failed\n");
        return 1;
    }
    my_buf = (char *)my_shm.addr;

    printf("fbox: ready\n");

    int32_t zzsh_pid = spawn_from_sd("bin/zzsh", "zzsh");
    if (zzsh_pid < 0)
        zzsh_pid = spawn_from_sd("/bin/zzsh", "zzsh");
    if (zzsh_pid < 0)
        printf("fbox: failed to spawn /bin/zzsh\n");
    else
        printf("fbox: spawned zzsh pid=%d\n", zzsh_pid);

    while (1) {
        zuzu_ipcmsg_t msg = _recv(my_port);
        uint32_t reply_h = (uint32_t)msg.r0;
        uint32_t sender  = msg.r1;
        uint32_t cmd     = msg.r2;
        uint32_t arg     = msg.r3;
        handle_request(reply_h, sender, cmd, arg);
    }
}
