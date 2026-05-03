#include "zuzu.h"
#include "zuzu/protocols/nt_protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <mem.h>

#include "zuzu/protocols/sysd_protocol.h"
#include "zuzu/protocols/fbox_protocol.h"
#include <zuzu/ipcx.h>
#include <cpio.h>
#include <zmalloc.h>
#include "den.h"
#include <zuzu/syspage.h>
#include "exec.h"
#include "sysd.h"

static nt_entry_t registry_table[SYSD_MAX_SERVICES];
static int32_t port;
static int32_t cached_fbox_handle = -1;
static char   *cached_fbox_buf    = NULL;

static inline void name_u32_to_chars(uint32_t name_u32, char out[SYSD_NAME_LEN]) {
    out[0] = (char)((name_u32 >> 0)  & 0xFF);
    out[1] = (char)((name_u32 >> 8)  & 0xFF);
    out[2] = (char)((name_u32 >> 16) & 0xFF);
    out[3] = (char)((name_u32 >> 24) & 0xFF);
}

static int name_equals_u32(const char name[SYSD_NAME_LEN], uint32_t name_u32) {
    char tmp[SYSD_NAME_LEN];
    name_u32_to_chars(name_u32, tmp);
    for (int i = 0; i < SYSD_NAME_LEN; i++) {
        if (name[i] != tmp[i]) return 0;
    }
    return 1;
}

int nt_setup(void) {
    port = _port_create();
    if (port < 0)
        return port;

    for (int i = 0; i < SYSD_MAX_SERVICES; i++) {
        registry_table[i].handle = 0;
        registry_table[i].pid = 0;
        for (int j = 0; j < SYSD_NAME_LEN; j++) registry_table[i].name[j] = 0;
    }

    den_init(_getpid());
    return 0;
}

static int nt_register(uint32_t name_u32, uint32_t handle,
                       uint32_t pid, uint32_t den_id) {
    if (handle == 0) return NT_REG_FAIL;

    if (den_id != 0 && !den_has_member(den_id, pid))
        return NT_REG_FAIL;

    for (int i = 0; i < SYSD_MAX_SERVICES; i++) {
        if (registry_table[i].handle != 0 &&
            registry_table[i].den_id == den_id &&
            name_equals_u32(registry_table[i].name, name_u32)) {
            if (registry_table[i].pid == pid) {
                registry_table[i].handle = handle;
                return NT_REG_OK;
            }
            return NT_REG_FAIL;
        }
    }

    for (int i = 0; i < SYSD_MAX_SERVICES; i++) {
        if (registry_table[i].handle == 0) {
            name_u32_to_chars(name_u32, registry_table[i].name);
            registry_table[i].handle = handle;
            registry_table[i].pid = pid;
            registry_table[i].den_id = den_id;
            return NT_REG_OK;
        }
    }

    return NT_REG_FAIL;
}

static int nt_lookup(uint32_t name_u32, uint32_t requester_pid,
                     uint32_t *out_handle, uint32_t *out_pid) {
    for (int i = 0; i < SYSD_MAX_SERVICES; i++) {
        if (registry_table[i].handle == 0) continue;
        if (!name_equals_u32(registry_table[i].name, name_u32)) continue;

        uint32_t did = registry_table[i].den_id;
        if (did != 0 && !den_has_member(did, requester_pid))
            continue;

        *out_handle = registry_table[i].handle;
        *out_pid    = registry_table[i].pid;
        return NT_LU_OK;
    }
    return NT_LU_NOMATCH;
}

static void scrub_pid(uint32_t pid) {
    for (int i = 0; i < SYSD_MAX_SERVICES; i++) {
        if (registry_table[i].handle == 0) continue;
        if (registry_table[i].pid != pid) continue;

        registry_table[i].handle = 0;
        registry_table[i].pid = 0;
        registry_table[i].den_id = 0;
        for (int j = 0; j < SYSD_NAME_LEN; j++)
            registry_table[i].name[j] = 0;
    }
    den_scrub_pid(pid);
}

static void nt_handle_msg(zuzu_ipcmsg_t msg) {
    if (msg.r2 >= sizeof(exec_request_hdr_t) && msg.r2 <= IPCX_BUF_SIZE &&
        ((exec_request_hdr_t *)IPCX_BUF_VA)->cmd == SYSD_EXEC) {
        uint32_t reply_handle = (uint32_t)msg.r0;
        uint32_t req_len = msg.r2;
        exec_request_hdr_t *hdr = (exec_request_hdr_t *)IPCX_BUF_VA;

        size_t path_off = sizeof(exec_request_hdr_t);
        size_t path_bytes = (size_t)hdr->path_len + 1;
        if (path_bytes == 0 || path_off + path_bytes > req_len ||
            ((char *)IPCX_BUF_VA)[path_off + hdr->path_len] != '\0') {
            _reply(reply_handle, (uint32_t)EXEC_ENOENT, 0, 0);
            return;
        }

        const char *path = (const char *)IPCX_BUF_VA + path_off;
        const char *argbuf = (const char *)IPCX_BUF_VA + path_off + path_bytes;
        size_t argbuf_len = req_len - path_off - path_bytes;

        /* --- lazy-init fbox connection (once) --- */
        if (cached_fbox_handle < 0) {
            uint32_t fbox_h = 0, fbox_p = 0;
            if (nt_lookup(nt_pack("fbox"), _getpid(), &fbox_h, &fbox_p) != NT_LU_OK) {
                _reply(reply_handle, (uint32_t)EXEC_ENOENT, 0, 0);
                return;
            }
            cached_fbox_handle = (int32_t)fbox_h;

            zuzu_ipcmsg_t r = _call(cached_fbox_handle, FBOX_GET_BUF, 0, 0);
            if ((int32_t)r.r1 != FBOX_OK) {
                cached_fbox_handle = -1;
                _reply(reply_handle, (uint32_t)EXEC_EIO, 0, 0);
                return;
            }

            cached_fbox_buf = (char *)_attach((int32_t)r.r2);
            if ((intptr_t)cached_fbox_buf <= 0) {
                cached_fbox_handle = -1;
                cached_fbox_buf = NULL;
                _reply(reply_handle, (uint32_t)EXEC_EIO, 0, 0);
                return;
            }
        }
        char *fbox_buf = cached_fbox_buf;
        zuzu_ipcmsg_t r;

        size_t plen = strlen(path);
        if (plen == 0 || plen >= 4096) {
            _reply(reply_handle, (uint32_t)EXEC_ENOENT, 0, 0);
            return;
        }

        memcpy(fbox_buf, path, plen + 1);
        r = _call(cached_fbox_handle, FBOX_STAT, 0, 0);
        if ((int32_t)r.r1 != FBOX_OK) {
            _reply(reply_handle, (uint32_t)EXEC_ENOENT, 0, 0);
            return;
        }

        fbox_stat_t *st = (fbox_stat_t *)fbox_buf;
        uint32_t file_size = st->size;
        if (file_size == 0 || st->is_dir) {
            _reply(reply_handle, (uint32_t)EXEC_EBADELF, 0, 0);
            return;
        }

        memcpy(fbox_buf, path, plen + 1);
        r = _call(cached_fbox_handle, FBOX_OPEN, FAT32_MODE_READ, 0);
        if ((int32_t)r.r1 != FBOX_OK) {
            _reply(reply_handle, (uint32_t)EXEC_EIO, 0, 0);
            return;
        }

        uint32_t fd = r.r2;
        uint8_t *elf = (uint8_t *)zmalloc(file_size);
        if (!elf) {
            _call(cached_fbox_handle, FBOX_CLOSE, fd, 0);
            _reply(reply_handle, (uint32_t)EXEC_ENOMEM, 0, 0);
            return;
        }

        uint32_t total = 0;
        while (total < file_size) {
            uint32_t chunk = file_size - total;
            if (chunk > 4096)
                chunk = 4096;

            r = _call(cached_fbox_handle, FBOX_READ, FBOX_PACK_RW(fd, chunk), 0);
            if ((int32_t)r.r1 != FBOX_OK || r.r2 == 0)
                break;

            memcpy(elf + total, fbox_buf, r.r2);
            total += r.r2;
        }
        _call(cached_fbox_handle, FBOX_CLOSE, fd, 0);

        if (total != file_size) {
            zfree(elf);
            _reply(reply_handle, (uint32_t)EXEC_EIO, 0, 0);
            return;
        }

        exec_reply_t reply;
        int rc = exec_inject((uint32_t)hdr->task_handle, elf, file_size,
                             argbuf_len ? argbuf : NULL, argbuf_len,
                             hdr->argc, &reply);
        zfree(elf);
        if (rc != 0) {
            _reply(reply_handle, (uint32_t)EXEC_EBADELF, 0, 0);
            return;
        }

        memcpy((void *)IPCX_BUF_VA, &reply, sizeof(reply));
        _replyx(reply_handle, sizeof(reply));
        return;
    }

    uint32_t sender = 0;
    uint32_t reply_handle = 0;
    uint32_t raw_command = 0;
    uint32_t command = 0;
    uint32_t den_id = 0;
    uint32_t name_u32 = 0;
    uint32_t arg = 0;
    int needs_reply = 0;

    uint32_t r2_cmd = msg.r2 & 0xFF;
    if (r2_cmd == NT_LOOKUP || r2_cmd == DEN_CREATE ||
        r2_cmd == DEN_INVITE || r2_cmd == DEN_KICK ||
        r2_cmd == DEN_MYDEN || r2_cmd == DEN_MYDEN_COUNT ||
        r2_cmd == DEN_MYDEN_AT || r2_cmd == SYSD_EXEC) {
        reply_handle = (uint32_t)msg.r0;
        sender       = msg.r1;
        raw_command  = msg.r2;
        name_u32     = msg.r3;
        arg          = msg.r3;
        needs_reply  = 1;
    } else {
        sender      = (uint32_t)msg.r0;
        raw_command = msg.r1;
        name_u32    = msg.r2;
        arg         = msg.r3;
        needs_reply = 0;
    }

    command = raw_command & 0xFF;
    den_id  = raw_command >> 8;

    int status = NT_BADCMD;
    uint32_t out_handle = 0;
    uint32_t out_pid = 0;

    if (command == NT_REGISTER) {
        status = nt_register(name_u32, arg, sender, den_id);

    } else if (command == NT_LOOKUP) {
        status = nt_lookup(name_u32, sender, &out_handle, &out_pid);
        if (status == NT_LU_OK) {
            int32_t slot = _port_grant((int32_t)out_handle, (int32_t)sender);
            if (slot < 0)
                status = NT_LU_NOMATCH;
            else
                out_handle = (uint32_t)slot;
        }

    } else if (command == DEN_CREATE) {
        int rc = den_create(sender, name_u32);
        if (rc >= 0) {
            out_handle = (uint32_t)rc;
            status = DEN_OK;
        } else {
            status = rc;
        }

    } else if (command == DEN_INVITE) {
        uint32_t target_pid = name_u32;
        if (!den_is_owner(den_id, sender))
            status = DEN_FAIL;
        else
            status = den_add_member(den_id, target_pid);

    } else if (command == DEN_KICK) {
        uint32_t target_pid = name_u32;
        if (!den_is_owner(den_id, sender))
            status = DEN_FAIL;
        else
            status = den_remove_member(den_id, target_pid);

    } else if (command == DEN_MYDEN) {
        uint32_t did = den_first_for_pid(sender);
        if (did != 0) {
            out_handle = did;
            out_pid = den_count_for_pid(sender);
            status = DEN_OK;
        } else {
            status = DEN_FAIL;
        }

    } else if (command == DEN_MYDEN_COUNT) {
        out_handle = den_count_for_pid(sender);
        status = DEN_OK;

    } else if (command == DEN_MYDEN_AT) {
        uint32_t did = den_for_pid_at(sender, name_u32);
        if (did != 0) {
            out_handle = did;
            status = DEN_OK;
        } else {
            status = DEN_FAIL;
        }

    } else if (command == SYSD_EXEC) {
        /* TODO: read ELF from fbox, exec_inject, reply with kickstart args */
    }

    if (needs_reply)
        _reply(reply_handle, (uint32_t)status, out_handle, out_pid);
}

#define WAIT_TIMEOUT_MS 30000u
#define WAIT_SLICE_MS   10u

static bool wait_for_service(uint32_t name_u32) {
    uint32_t handle = 0, pid = 0, waited_ms = 0;

    while (nt_lookup(name_u32, _getpid(), &handle, &pid) != NT_LU_OK &&
           waited_ms < WAIT_TIMEOUT_MS) {
        int32_t dead = _wait(-1, NULL, WNOHANG);
        if (dead > 0) scrub_pid((uint32_t)dead);

        zuzu_ipcmsg_t msg = _recv_timeout(port, WAIT_SLICE_MS);
        if (msg.r0 >= 0)
            nt_handle_msg(msg);
        waited_ms += WAIT_SLICE_MS;
    }

    return nt_lookup(name_u32, _getpid(), &handle, &pid) == NT_LU_OK;
}

void sysd_loop(void)
{
    while (1) {
        int32_t dead = _wait(-1, NULL, WNOHANG);
        if (dead > 0) scrub_pid((uint32_t)dead);
        nt_handle_msg(_recv(port));
    }
}

/* ================================================================
 *  Boot sequence
 * ================================================================ */

#define MAX_BOOT_ENTRIES 16

typedef struct {
    char          path[64];
    char          name[32];
    const void   *elf_data;     /* into CPIO mapping; NULL if SD-only */
    size_t        elf_size;
    int32_t       task_handle;
    uint32_t      pid;
    exec_reply_t  reply;
    bool          in_cpio;
    bool          injected;
} boot_entry_t;

static boot_entry_t boot_entries[MAX_BOOT_ENTRIES];
static int           boot_count;

static char deferred_paths[MAX_BOOT_ENTRIES][64];
static int  deferred_count;

static const char *basename(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') b = p + 1;
    return b;
}

static bool is_storage_member(const char *name)
{
    return strcmp(name, "zusd")   == 0 ||
           strcmp(name, "fat32d") == 0 ||
           strcmp(name, "fbox")   == 0;
}

static bool role_is_kernel(const char *r, size_t len)
{
    return (len == 4 && memcmp(r, "init", 4) == 0) ||
           (len == 3 && memcmp(r, "dev",  3) == 0) ||
           (len == 5 && memcmp(r, "devmgr", 5) == 0);
}

static void parse_manifest(const char *data, size_t size,
                           const void *cpio, size_t cpio_size)
{
    const char *p   = data;
    const char *end = data + size;

    boot_count     = 0;
    deferred_count = 0;

    while (p < end && boot_count < MAX_BOOT_ENTRIES) {
        const char *eol = p;
        while (eol < end && *eol != '\n') eol++;

        size_t ll = (size_t)(eol - p);
        if (ll == 0 || p[0] == '#') { p = eol + 1; continue; }

        while (ll > 0 && (p[ll-1] == '\r' || p[ll-1] == ' ' || p[ll-1] == '\t'))
            ll--;

        /* find pipe */
        size_t pipe = 0;
        while (pipe < ll && p[pipe] != '|') pipe++;
        if (pipe == 0 || pipe >= ll) { p = eol + 1; continue; }

        const char *ps = p;          size_t pl = pipe;
        const char *rs = p+pipe+1;   size_t rl = ll-pipe-1;

        while (pl > 0 && (ps[pl-1]==' '||ps[pl-1]=='\t')) pl--;
        while (rl > 0 && (*rs==' '||*rs=='\t')) { rs++; rl--; }
        while (rl > 0 && (rs[rl-1]==' '||rs[rl-1]=='\t')) rl--;

        if (role_is_kernel(rs, rl)) { p = eol + 1; continue; }
        if (pl >= 64)               { p = eol + 1; continue; }

        /* normalise path: bare name -> bin/<name> */
        char full[64];
        if (memchr(ps, '/', pl)) {
            memcpy(full, ps, pl);
            full[pl] = '\0';
        } else {
            memcpy(full, "bin/", 4);
            memcpy(full + 4, ps, pl);
            full[4 + pl] = '\0';
        }

        const void *elf = NULL;
        size_t      esz = 0;

        if (cpio_find(cpio, cpio_size, full, &elf, &esz)) {
            boot_entry_t *e = &boot_entries[boot_count++];
            strcpy(e->path, full);
            const char *bn = basename(full);
            strncpy(e->name, bn, sizeof(e->name) - 1);
            e->name[sizeof(e->name) - 1] = '\0';
            e->elf_data    = elf;
            e->elf_size    = esz;
            e->task_handle = -1;
            e->pid         = 0;
            e->in_cpio     = true;
            e->injected    = false;
        } else if (deferred_count < MAX_BOOT_ENTRIES) {
            strcpy(deferred_paths[deferred_count++], full);
        }

        p = eol + 1;
    }
}

int main(void)
{
    const zuzu_syspage_t *sp = (const zuzu_syspage_t *)SYSPAGE_VA;
    const void *initrd  = (const void *)INITRD_BASE;
    uint32_t initrd_sz  = sp->initrd_size;

    /* ---- nametable ---- */

    if (nt_setup() < 0)
        return 1;
    nt_register(nt_pack(NT_NAME_SYS), (uint32_t)port, _getpid(), 0);

    /* ---- read boot manifest from CPIO ---- */

    const void *mdata;
    size_t      msize;
    if (!cpio_find(initrd, initrd_sz, "boot.manifest", &mdata, &msize))
        return 1;

    parse_manifest((const char *)mdata, msize, initrd, initrd_sz);

    /* ---- tspawn + inject every CPIO-resident program ---- */

    for (int i = 0; i < boot_count; i++) {
        boot_entry_t *e = &boot_entries[i];
        if (!e->in_cpio)
            continue;

        tspawn_result_t ts = _tspawn(e->name);
        if (ts.task_handle < 0)
            continue;

        e->task_handle = ts.task_handle;
        e->pid         = ts.pid;

        if (exec_inject((uint32_t)ts.task_handle,
                        e->elf_data, e->elf_size,
                        NULL, 0, 0, &e->reply) != 0)
            continue;

        e->injected = true;
    }

    /* ---- storage den ---- */

    int disk_den = den_create(_getpid(), nt_pack("disk"));
    if (disk_den >= 0) {
        for (int i = 0; i < boot_count; i++) {
            if (!boot_entries[i].injected) continue;
            if (is_storage_member(boot_entries[i].name))
                den_add_member((uint32_t)disk_den, boot_entries[i].pid);
        }
    }

    /* ---- kickstart ---- */

    for (int i = 0; i < boot_count; i++) {
        boot_entry_t *e = &boot_entries[i];
        if (!e->injected) continue;

        kickstart_args_t ks = {
            .task_handle = (uint32_t)e->task_handle,
            .entry       = e->reply.entry,
            .sp          = e->reply.sp,
            .r0_val      = e->reply.argc,
            .r1_val      = e->reply.argv_va,
        };
        _kickstart(&ks);
    }

    /* devmgr is kernel-spawned and already running; wait for it to
     * register so other services can find it once they start. */
    wait_for_service(nt_pack("devm"));

    // wait for fbox so we can spawn deferred entries through it once it's ready
    wait_for_service(nt_pack("fbox"));

    sysd_loop();
    return 0;
}
