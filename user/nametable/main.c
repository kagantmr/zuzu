#include "nametable.h"
#include "zuzu.h"
#include "zuzu/protocols/nt_protocol.h"
#include <stdint.h>
#include <stddef.h>

static nt_entry_t registry_table[NT_MAX_SERVICES];

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

static void nt_setup(void) {
    for (int i = 0; i < NT_MAX_SERVICES; i++) {
        registry_table[i].handle = 0;         // 0 == empty slot
        registry_table[i].pid = 0;
        for (int j = 0; j < NT_NAME_LEN; j++) registry_table[i].name[j] = 0;
    }
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

int main(void) {
    nt_setup();

    while (1) {
        zuzu_ipcmsg_t msg = _recv(NT_PORT);

        uint32_t sender  = msg.r0;  
        uint32_t command = msg.r1;
        uint32_t name_u32 = msg.r2;
        uint32_t arg      = msg.r3;

        int status = NT_BADCMD;
        uint32_t out_handle = 0;

        uint32_t out_pid = 0;

        if (command == NT_REGISTER) {
            status = nt_register(name_u32, arg, sender);
        } else if (command == NT_LOOKUP) {
            status = nt_lookup(name_u32, &out_handle, &out_pid);
            if (status == NT_LU_OK) {
                int32_t slot = _port_grant(out_handle, sender);
                if (slot < 0) status = NT_LU_NOMATCH;  // grant failed
                out_handle = slot;  // tell requester THEIR slot index
            }
        }

        // Reply to sender 
        // Reply payload is r1=status, r2=handle, r3=0
        _reply(sender, (uint32_t)status, out_handle, out_pid);
    }
}