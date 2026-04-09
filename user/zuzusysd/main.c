
#include "zuzu.h"
#include "zuzu/protocols/nt_protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include "den.h"
#include "zuzusysd.h"

static nt_entry_t registry_table[SYSD_MAX_SERVICES];
static int32_t port;

static inline void name_u32_to_chars(uint32_t name_u32, char out[SYSD_NAME_LEN]) {
    // LE packing
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
    if (port < 0) {
        return port;
    }

    for (int i = 0; i < SYSD_MAX_SERVICES; i++) {
        registry_table[i].handle = 0;         // 0 == empty slot
        registry_table[i].pid = 0;
        for (int j = 0; j < SYSD_NAME_LEN; j++) registry_table[i].name[j] = 0;
    }
    
    den_init(_getpid());

    return 0;
}

static int nt_register(uint32_t name_u32, uint32_t handle,
                       uint32_t pid, uint32_t den_id) {
    if (handle == 0) return NT_REG_FAIL;

    // non-global: sender must be a member
    if (den_id != 0 && !den_has_member(den_id, pid))
        return NT_REG_FAIL;

    // reject duplicates within the same den, but allow same process to refresh handle
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
        if (registry_table[i].handle == 0) {
            continue;
        }
        if (registry_table[i].pid != pid) {
            continue;
        }

        registry_table[i].handle = 0;
        registry_table[i].pid = 0;
        registry_table[i].den_id = 0;
        for (int j = 0; j < SYSD_NAME_LEN; j++) {
            registry_table[i].name[j] = 0;
        }
    }

    den_scrub_pid(pid);
}

static void nt_handle_msg(zuzu_ipcmsg_t msg) {
    uint32_t sender = 0;
    uint32_t reply_handle = 0;
    uint32_t raw_command = 0;
    uint32_t command = 0;
    uint32_t den_id = 0;
    uint32_t name_u32 = 0;
    uint32_t arg = 0;
    int needs_reply = 0;

    /*
     * Routing: den commands + NT_LOOKUP arrive via _call.
     * NT_REGISTER arrives via _send.
     *
     * _call delivery:  r0=reply_handle, r1=sender_pid, r2=w1, r3=w2
     * _send delivery:  r0=sender_pid,   r1=w1,         r2=w2, r3=w3
     *
     * For _call, command is in msg.r2. For _send, command is in msg.r1.
     * We detect call mode by checking if msg.r2 matches a known call-mode command.
     */
    uint32_t r2_cmd = msg.r2 & 0xFF;
    if (r2_cmd == NT_LOOKUP || r2_cmd == DEN_CREATE ||
        r2_cmd == DEN_INVITE || r2_cmd == DEN_KICK ||
        r2_cmd == DEN_MYDEN || r2_cmd == DEN_MYDEN_COUNT ||
        r2_cmd == DEN_MYDEN_AT) {
        reply_handle = (uint32_t)msg.r0;
        sender       = msg.r1;
        raw_command  = msg.r2;
        name_u32     = msg.r3;    // doubles as arg for den commands
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
            if (slot < 0) {
                status = NT_LU_NOMATCH;
            } else {
                out_handle = (uint32_t)slot;
            }
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
        if (!den_is_owner(den_id, sender)) {
            status = DEN_FAIL;
        } else {
            status = den_add_member(den_id, target_pid);
        }

    } else if (command == DEN_KICK) {
        uint32_t target_pid = name_u32;
        if (!den_is_owner(den_id, sender)) {
            status = DEN_FAIL;
        } else {
            status = den_remove_member(den_id, target_pid);
        }
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

    while (nt_lookup(name_u32, _getpid(), &handle, &pid) != NT_LU_OK && time < WAIT_TIMEOUT) {
        int32_t dead = _wait(-1, NULL, WNOHANG);
        if (dead > 0) {
            scrub_pid((uint32_t)dead);
        }
        nt_handle_msg(_recv(port));
        time += 1;
    }
}

void sysd_loop(void)
{
    while (1) {
        int32_t dead = _wait(-1, NULL, WNOHANG);
        if (dead > 0) {
            scrub_pid((uint32_t)dead);
        }
        nt_handle_msg(_recv(port));
    }
}

int main(void) {
    if (nt_setup() < 0) {
        return 1;
    }

    nt_register(nt_pack(NT_NAME_SYS), (uint32_t)port, _getpid(), 0);

    wait_for_service(nt_pack("devm"));
    wait_for_service(nt_pack("uart"));

    wait_for_service(nt_pack("zusd"));

    wait_for_service(nt_pack("fat3"));

    wait_for_service(nt_pack("fbox"));

    
    sysd_loop();
    return 0;
}
