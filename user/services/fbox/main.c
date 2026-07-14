#include <zuzu/protocols/fat32d_protocol.h>
#include <zuzu/protocols/fbox_protocol.h>
#include <zuzu/protocols/nt_protocol.h>
#include <zuzu/service.h>
#include <zuzu/zuzu.h>
#include <stdio.h>
#include <string.h>
#include <mem.h>
#include <stdbool.h>
#include <malloc.h>
#include <zuzu/user_layout.h>

static int32_t fat32d_port = -1;
static void *fat32d_buf = NULL;    /* shmem shared with fat32d */

#define MAX_FBOX_CLIENTS 16

typedef struct {
    uint32_t pid;
    handle_t shm_handle;
    char *buf;
} client_buf_t;

static client_buf_t clients[MAX_FBOX_CLIENTS];

/* forward declarations so worker can call these helpers */
static void proxy_open(uint32_t reply_h, uint32_t arg, char *client_buf);
static void proxy_read(uint32_t reply_h, uint32_t arg, char *client_buf);
static void proxy_write(uint32_t reply_h, uint32_t arg, char *client_buf);
static void proxy_close(uint32_t reply_h, uint32_t arg);
static void proxy_readdir(uint32_t reply_h, char *client_buf);
static void proxy_stat(uint32_t reply_h, char *client_buf);

/* Job queue + worker to offload blocking proxy calls to fat32d. */
#define FBOX_JOB_QUEUE_SZ 16
typedef struct {
    uint32_t cmd;
    uint32_t arg;
    uint32_t reply_h;
    uint32_t sender;
    int client_idx;
} fbox_job_t;

static fbox_job_t fbox_jobs[FBOX_JOB_QUEUE_SZ];
static unsigned fbox_job_head = 0;
static unsigned fbox_job_tail = 0;
static int32_t fbox_worker_ntfn = -1;

static void fbox_worker(void *arg)
{
    (void)arg;
    while (1) {
        _ntfn_wait((uint32_t)fbox_worker_ntfn, TIMEOUT_INFINITE);
        while (fbox_job_head != fbox_job_tail) {
            fbox_job_t job = fbox_jobs[fbox_job_tail];
            fbox_job_tail = (fbox_job_tail + 1) % FBOX_JOB_QUEUE_SZ;

            client_buf_t *client = NULL;
            if (job.client_idx >= 0 && job.client_idx < MAX_FBOX_CLIENTS)
                client = &clients[job.client_idx];

            switch (job.cmd) {
            case FBOX_OPEN:
                if (client) proxy_open(job.reply_h, job.arg, client->buf);
                else _reply(job.reply_h, (uint32_t)ERR_NOENT, 0, 0);
                break;
            case FBOX_READ:
                if (client) proxy_read(job.reply_h, job.arg, client->buf);
                else _reply(job.reply_h, (uint32_t)ERR_NOENT, 0, 0);
                break;
            case FBOX_WRITE:
                if (client) proxy_write(job.reply_h, job.arg, client->buf);
                else _reply(job.reply_h, (uint32_t)ERR_NOENT, 0, 0);
                break;
            case FBOX_CLOSE:
                proxy_close(job.reply_h, job.arg);
                break;
            case FBOX_READDIR:
                if (client) proxy_readdir(job.reply_h, client->buf);
                else _reply(job.reply_h, (uint32_t)ERR_NOENT, 0, 0);
                break;
            case FBOX_STAT:
                if (client) proxy_stat(job.reply_h, client->buf);
                else _reply(job.reply_h, (uint32_t)ERR_NOENT, 0, 0);
                break;
            default:
                _reply(job.reply_h, (uint32_t)ERR_NOSYS, 0, 0);
                break;
            }
        }
    }
}

/* Copy client shmem -> fat32d shmem, forward command, copy result back.
 * For commands where the client writes data IN (path, write data):
 *   copy my_buf -> fat32d_buf before the call.
 * For commands where fat32d writes data OUT (read data, dirents, stat):
 *   copy fat32d_buf -> my_buf after the call.
 */

static void proxy_open(uint32_t reply_h, uint32_t arg, char *client_buf)
{
    /* path in client_buf -> fat32d_buf */
    size_t plen = strlen(client_buf);
    memcpy(fat32d_buf, client_buf, plen + 1);

    msg_t r = _call(fat32d_port, FAT32_OPEN, arg, 0);
    _reply(reply_h, r.r1, r.r2, 0);
}

static void proxy_read(uint32_t reply_h, uint32_t arg, char *client_buf)
{
    msg_t r = _call(fat32d_port, FAT32_READ, arg, 0);

    /* data in fat32d_buf -> my_buf */
    uint32_t count = FAT32_RW_COUNT(arg);
    if (count > 32768) count = 32768;
    uint32_t got = r.r2;
    if (got > count) got = count;
    if ((int32_t)r.r1 == ZUZU_OK && got > 0)
        memcpy(client_buf, fat32d_buf, got);

    _reply(reply_h, r.r1, r.r2, 0);
}

static void proxy_write(uint32_t reply_h, uint32_t arg, char *client_buf)
{
    /* data in my_buf -> fat32d_buf */
    uint32_t count = FAT32_RW_COUNT(arg);
    if (count > 32768) count = 32768;
    memcpy(fat32d_buf, client_buf, count);

    msg_t r = _call(fat32d_port, FAT32_WRITE, arg, 0);
    _reply(reply_h, r.r1, r.r2, 0);
}

static void proxy_close(uint32_t reply_h, uint32_t arg)
{
    msg_t r = _call(fat32d_port, FAT32_CLOSE, arg, 0);
    _reply(reply_h, r.r1, 0, 0);
}

static void proxy_readdir(uint32_t reply_h, char *client_buf)
{
    /* path in my_buf -> fat32d_buf */
    size_t plen = strlen(client_buf);
    memcpy(fat32d_buf, client_buf, plen + 1);

    msg_t r = _call(fat32d_port, FAT32_READDIR, 0, 0);

    /* dirents in fat32d_buf -> my_buf */
    if ((int32_t)r.r1 == ZUZU_OK && r.r2 > 0) {
        uint32_t bytes = r.r2 * sizeof(fat32_dirent_t);
        if (bytes > 32768) bytes = 32768;
        memcpy(client_buf, fat32d_buf, bytes);
    }

    _reply(reply_h, r.r1, r.r2, 0);
}

static void proxy_stat(uint32_t reply_h, char *client_buf)
{
    /* path in my_buf -> fat32d_buf */
    size_t plen = strlen(client_buf);
    memcpy(fat32d_buf, client_buf, plen + 1);

    msg_t r = _call(fat32d_port, FAT32_STAT, 0, 0);

    /* stat result in fat32d_buf -> my_buf */
    if ((int32_t)r.r1 == ZUZU_OK)
        memcpy(client_buf, fat32d_buf, sizeof(fat32_stat_t));

    _reply(reply_h, r.r1, 0, 0);
}

static client_buf_t *client_find(uint32_t pid)
{
    for (int i = 0; i < MAX_FBOX_CLIENTS; i++) {
        if (clients[i].pid == pid)
            return &clients[i];
    }
    return NULL;
}

static client_buf_t *client_alloc(uint32_t pid)
{
    for (int i = 0; i < MAX_FBOX_CLIENTS; i++) {
        if (clients[i].pid == 0) {
            handle_t shm_h = _shm_create(32768);
            if (shm_h < 0)
                return NULL;
            char *shm_buf = (char *)_memmap(shm_h, 0, VM_PROT_RW, 0);
            if (_ptr_is_err(shm_buf))
                return NULL;
            clients[i].pid = pid;
            clients[i].shm_handle = shm_h;
            clients[i].buf = shm_buf;
            return &clients[i];
        }
    }
    return NULL;
}

static client_buf_t *client_get(uint32_t pid, bool create)
{
    client_buf_t *c = client_find(pid);
    if (c || !create)
        return c;
    return client_alloc(pid);
}

static void handle_get_buf(uint32_t reply_h, uint32_t sender)
{
    client_buf_t *client = client_get(sender, true);
    if (!client) {
        _reply(reply_h, (uint32_t)ERR_NOMEM, 0, 0);
        return;
    }

    int32_t slot = _grant(client->shm_handle, (int32_t)sender);
    if (slot < 0)
        _reply(reply_h, (uint32_t)slot, 0, 0);
    else
        _reply(reply_h, ZUZU_OK, (uint32_t)slot, 0);
}

static void handle_request(uint32_t reply_h, uint32_t sender,
                           uint32_t cmd, uint32_t arg)
{
    if (cmd == FBOX_GET_BUF) {
        handle_get_buf(reply_h, sender);
        return;
    }

    client_buf_t *client = client_get(sender, false);
    if (!client || !client->buf) {
        _reply(reply_h, (uint32_t)ERR_NOENT, 0, 0);
        return;
    }

    switch (cmd) {
    case FBOX_OPEN:    proxy_open(reply_h, arg, client->buf);    break;
    case FBOX_READ:    proxy_read(reply_h, arg, client->buf);    break;
    case FBOX_WRITE:   proxy_write(reply_h, arg, client->buf);   break;
    case FBOX_CLOSE:   proxy_close(reply_h, arg);         break;
    case FBOX_READDIR: proxy_readdir(reply_h, client->buf);      break;
    case FBOX_STAT:    proxy_stat(reply_h, client->buf);         break;
    default:
        _reply(reply_h, (uint32_t)ERR_NOSYS, 0, 0);
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

    /* look up fat32d */
    fat32d_port = lookup_service("fat3");
    if (fat32d_port < 0) {
        printf("fbox: cannot find fat32d\n");
        return 1;
    }

    /* get fat32d's shmem buffer */
    msg_t r = _call(fat32d_port, FAT32_GET_BUF, 0, 0);
    if ((int32_t)r.r1 != 0) {
        printf("fbox: FAT32_GET_BUF failed\n");
        return 1;
    }
    fat32d_buf = _memmap((int32_t)r.r2, 0, VM_PROT_RW, 0);
    if ((intptr_t)fat32d_buf <= 0) {
        printf("fbox: attach failed\n");
        return 1;
    }

    /* publish globally only after all proxied backends and client shmem exist */
    int32_t global_slot = _grant(my_port, NAMETABLE_PID);
    if (global_slot < 0) {
        printf("fbox: global grant failed\n");
        return 1;
    }
    _send(NT_PORT, NT_REGISTER | (0 << 8), nt_pack("fbox"), (uint32_t)global_slot);

    /* create worker notification and spawn worker thread */
    fbox_worker_ntfn = _ntfn_create();
    if (fbox_worker_ntfn < 0) {
        printf("fbox: worker ntfn create failed\n");
        return 1;
    }

    void *wstack = malloc(USER_STACK_SIZE);
    if (!wstack) {
        printf("fbox: worker stack alloc failed\n");
        return 1;
    }

    tid_t wt = _tmake(fbox_worker, (void *)((char *)wstack + USER_STACK_SIZE), NULL);
    if ((int)wt < 0) {
        printf("fbox: worker spawn failed\n");
        return 1;
    }

    while (1) {
        msg_t msg = _recv(my_port);
        uint32_t reply_h = (uint32_t)msg.r0;
        uint32_t sender  = msg.r1;
        uint32_t cmd     = msg.r2;
        uint32_t arg     = msg.r3;
        handle_request(reply_h, sender, cmd, arg);
    }
}
