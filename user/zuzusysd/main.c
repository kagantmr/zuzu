#include "zuzusysd.h"
#include "zuzu.h"
#include "zuzu/protocols/nt_protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

static nt_entry_t registry_table[NT_MAX_SERVICES];
static int32_t port;

#define LOG_LIT(s) printf("%s", (s))

static inline void name_u32_to_chars(uint32_t name_u32, char out[NT_NAME_LEN]) {
    // LE packing
    out[0] = (char)((name_u32 >> 0)  & 0xFF);
    out[1] = (char)((name_u32 >> 8)  & 0xFF);
    out[2] = (char)((name_u32 >> 16) & 0xFF);
    out[3] = (char)((name_u32 >> 24) & 0xFF);
}

static int name_equals_u32(const char name[NT_NAME_LEN], uint32_t name_u32) {
    char tmp[NT_NAME_LEN];
    name_u32_to_chars(name_u32, tmp);
    for (int i = 0; i < NT_NAME_LEN; i++) {
        if (name[i] != tmp[i]) return 0;
    }
    return 1;
}

int nt_setup(void) {
    port = _port_create();
    if (port < 0) {
        return port;
    }

    for (int i = 0; i < NT_MAX_SERVICES; i++) {
        registry_table[i].handle = 0;         // 0 == empty slot
        registry_table[i].pid = 0;
        for (int j = 0; j < NT_NAME_LEN; j++) registry_table[i].name[j] = 0;
    }
    return 0;
}

static int nt_register(uint32_t name_u32, uint32_t handle, uint32_t pid) {
    if (handle == 0) return NT_REG_FAIL;

    // Reject duplicate names (or change this to "update" if you want)
    for (int i = 0; i < NT_MAX_SERVICES; i++) {
        if (registry_table[i].handle != 0 && name_equals_u32(registry_table[i].name, name_u32)) {
            return NT_REG_FAIL;
        }
    }

    // Insert into first free slot
    for (int i = 0; i < NT_MAX_SERVICES; i++) {
        if (registry_table[i].handle == 0) {
            name_u32_to_chars(name_u32, registry_table[i].name);
            registry_table[i].handle = handle;
            registry_table[i].pid = pid;
            return NT_REG_OK;
        }
    }

    return NT_REG_FAIL; // table full
}

static int nt_lookup(uint32_t name_u32, uint32_t *out_handle, uint32_t *out_pid) {
    for (int i = 0; i < NT_MAX_SERVICES; i++) {
        if (registry_table[i].handle != 0 && name_equals_u32(registry_table[i].name, name_u32)) {
            *out_handle = registry_table[i].handle;
            *out_pid = registry_table[i].pid;
            return NT_LU_OK;
        }
    }
    *out_handle = 0;
    return NT_LU_NOMATCH;
}

static void nt_handle_msg(zuzu_ipcmsg_t msg) {
    uint32_t sender = 0;
    uint32_t reply_handle = 0;
    uint32_t command = 0;
    uint32_t name_u32 = 0;
    uint32_t arg = 0;
    int needs_reply = 0;

    // call mode: r0=reply_handle, r1=sender_pid, r2=command, r3=arg
    if (msg.r2 == NT_LOOKUP || msg.r2 == NT_REGISTER) {
        reply_handle = (uint32_t)msg.r0;
        sender = msg.r1;
        command = msg.r2;
        name_u32 = msg.r3;
        arg = 0;
        needs_reply = 1;
    } else {
        // send mode: r0=sender_pid, r1=command, r2=arg0, r3=arg1
        sender = (uint32_t)msg.r0;
        command = msg.r1;
        name_u32 = msg.r2;
        arg = msg.r3;
        needs_reply = 0;
    }

    int status = NT_BADCMD;
    uint32_t out_handle = 0;
    uint32_t out_pid = 0;

    if (command == NT_REGISTER) {
        status = nt_register(name_u32, arg, sender);
    } else if (command == NT_LOOKUP) {
        status = nt_lookup(name_u32, &out_handle, &out_pid);
        if (status == NT_LU_OK) {
            int32_t slot = _port_grant((int32_t)out_handle, (int32_t)sender);
            if (slot < 0) {
                status = NT_LU_NOMATCH;
            } else {
                out_handle = (uint32_t)slot;
            }
        }
    }

    if (needs_reply) {
        _reply(reply_handle, (uint32_t)status, out_handle, out_pid);
    }
}

#define WAIT_TIMEOUT 100000000u
static void wait_for_service(uint32_t name_u32) {
    uint32_t handle = 0;
    uint32_t pid = 0;
    uint32_t time = 0;

    while (nt_lookup(name_u32, &handle, &pid) != NT_LU_OK && time < WAIT_TIMEOUT) {
        _wait(-1, NULL, WNOHANG);
        nt_handle_msg(_recv(port));
        time += 1;
    }
}

void sysd_loop(void)
{
    while (1) {
        _wait(-1, NULL, WNOHANG);
        nt_handle_msg(_recv(port));
    }
}

int main(void) {
    if (nt_setup() < 0) {
        return 1;
    }

    nt_register(nt_pack(NT_NAME_SYS), (uint32_t)port, _getpid());

    wait_for_service(nt_pack("devm"));

    if (_spawn("bin/zuart", 9) < 0) {
        LOG_LIT("zuzusysd: failed to spawn zuart\n");
    }

    wait_for_service(nt_pack("uart"));

    if (_spawn("bin/zusd", 8) < 0) {
        LOG_LIT("zuzusysd: failed to spawn zusd\n");
    }

    wait_for_service(nt_pack("zusd"));

    if (_spawn("bin/zzsh", 8) < 0) {
        LOG_LIT("zuzusysd: failed to spawn zzsh\n");
    }
    
    sysd_loop();
    return 0;
}
