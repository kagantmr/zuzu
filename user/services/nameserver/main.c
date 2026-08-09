#include <zuzu/types.h>
#include <zuzu/msg.h>
#include "zuzu/protocols/nametable.h"
#include <stdint.h>

#define NS_MAX_SERVICES 64
#define NS_NAME_LEN 4

/* Registry entry: `handle` is always a slot in *our own* handle table —
 * every registrant grants us its serving port (ZuzuGrant(port, our pid))
 * before sending NT_REGISTER with the slot that grant landed at, so a
 * lookup can re-grant that same local handle out to whoever asked. */
typedef struct {
    char name[NS_NAME_LEN];
    uint32_t handle;
    uint32_t pid;
} nt_entry_t;

static nt_entry_t registry_table[NS_MAX_SERVICES];

static inline void name_u32_to_chars(uint32_t name_u32, char out[NS_NAME_LEN]) {
    out[0] = (char)((name_u32 >> 0)  & 0xFF);
    out[1] = (char)((name_u32 >> 8)  & 0xFF);
    out[2] = (char)((name_u32 >> 16) & 0xFF);
    out[3] = (char)((name_u32 >> 24) & 0xFF);
}

static int name_equals_u32(const char name[NS_NAME_LEN], uint32_t name_u32) {
    char tmp[NS_NAME_LEN];
    name_u32_to_chars(name_u32, tmp);
    for (int i = 0; i < NS_NAME_LEN; i++) {
        if (name[i] != tmp[i]) return 0;
    }
    return 1;
}

static int nt_register(uint32_t name_u32, uint32_t handle, uint32_t pid) {
    if (handle == 0) return NT_REG_FAIL;

    for (int i = 0; i < NS_MAX_SERVICES; i++) {
        if (registry_table[i].handle != 0 &&
            name_equals_u32(registry_table[i].name, name_u32)) {
            if (registry_table[i].pid == pid) {
                registry_table[i].handle = handle;
                return NT_REG_OK;
            }
            return NT_REG_FAIL;
        }
    }

    for (int i = 0; i < NS_MAX_SERVICES; i++) {
        if (registry_table[i].handle == 0) {
            name_u32_to_chars(name_u32, registry_table[i].name);
            registry_table[i].handle = handle;
            registry_table[i].pid = pid;
            return NT_REG_OK;
        }
    }

    return NT_REG_FAIL;
}

static int nt_lookup(uint32_t name_u32, uint32_t *out_handle, uint32_t *out_pid) {
    for (int i = 0; i < NS_MAX_SERVICES; i++) {
        if (registry_table[i].handle == 0) continue;
        if (!name_equals_u32(registry_table[i].name, name_u32)) continue;

        *out_handle = registry_table[i].handle;
        *out_pid    = registry_table[i].pid;
        return NT_LU_OK;
    }
    return NT_LU_NOMATCH;
}

static int nt_lookup_pid(uint32_t pid, uint32_t *out_handle, uint32_t *out_pid) {
    for (int i = 0; i < NS_MAX_SERVICES; i++) {
        if (registry_table[i].handle == 0) continue;
        if (registry_table[i].pid != pid) continue;

        *out_handle = registry_table[i].handle;
        *out_pid    = registry_table[i].pid;
        return NT_LU_OK;
    }
    return NT_LU_NOMATCH;
}

static void scrub_pid(uint32_t pid) {
    for (int i = 0; i < NS_MAX_SERVICES; i++) {
        if (registry_table[i].handle == 0) continue;
        if (registry_table[i].pid != pid) continue;

        registry_table[i].handle = 0;
        registry_table[i].pid = 0;
        for (int j = 0; j < NS_NAME_LEN; j++)
            registry_table[i].name[j] = 0;
    }
}

/* Mirrors sysd's old nt_handle_msg dispatch (minus SYSD_EXEC/DEN_*, which
 * never belonged to naming): NT_LOOKUP/NT_LOOKUP_PID arrive as a
 * ZuzuMsgCall (reply_handle/sender prefixed onto w0/w1, shifting the
 * client's own w1/w2 into w2/w3 — see the client-payload-shift comment on
 * NT_LOOKUP in nametable.h), everything else as a plain ZuzuMsgSend (w0 =
 * kernel-stamped sender, w1-w3 verbatim). The command/send disambiguation
 * peeks at the low byte of w2, which only works because every send-style
 * command here keeps small integers (matching a reserved id) out of that
 * position — NT_REGISTER puts a name there (printable ASCII, always a high
 * byte value) and NT_SCRUB_PID deliberately leaves it 0 rather than the
 * pid, since pids in this system are small enough to collide. */
static void nt_handle_msg(Message msg) {
    uint32_t sender = 0;
    uint32_t reply_handle = 0;
    uint32_t raw_command = 0;
    uint32_t name_u32 = 0;
    uint32_t arg = 0;
    int needs_reply = 0;

    uint32_t r2_cmd = msg.w2 & 0xFF;
    if (r2_cmd == NT_LOOKUP || r2_cmd == NT_LOOKUP_PID) {
        reply_handle = (uint32_t)msg.w0;
        sender       = msg.w1;
        raw_command  = msg.w2;
        name_u32     = msg.w3;
        arg          = msg.w3;
        needs_reply  = 1;
    } else {
        sender      = (uint32_t)msg.w0;
        raw_command = msg.w1;
        name_u32    = msg.w2;
        arg         = msg.w3;
        needs_reply = 0;
    }

    uint32_t command = raw_command & 0xFF;

    int status = NT_BADCMD;
    uint32_t out_handle = 0;
    uint32_t out_pid = 0;

    if (command == NT_REGISTER) {
        status = nt_register(name_u32, arg, sender);

    } else if (command == NT_LOOKUP) {
        status = nt_lookup(name_u32, &out_handle, &out_pid);
        if (status == NT_LU_OK) {
            int32_t slot = ZuzuGrant((Handle)out_handle, (Pid)sender);
            if (slot < 0) {
                status = NT_LU_NOMATCH;
                out_handle = 0;
                out_pid = 0;
            } else {
                out_handle = (uint32_t)slot;
            }
        }

    } else if (command == NT_LOOKUP_PID) {
        status = nt_lookup_pid(name_u32, &out_handle, &out_pid);

    } else if (command == NT_SCRUB_PID) {
        scrub_pid(arg);
        status = ZUZU_OK;
    }

    if (needs_reply)
        ZuzuMsgReply((Handle)reply_handle, (uint32_t)status, out_handle, out_pid);
}

int main(void) {
    while (1) {
        Message msg = ZuzuMsgRecv(NT_PORT, TIMEOUT_INFINITE);
        nt_handle_msg(msg);
    }
    return ZUZU_OK;
}
