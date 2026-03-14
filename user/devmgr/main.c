#include "devmgr.h"
#include <string.h>
#include <mem.h>

#define MAX_DRIVERS 64

typedef struct {
    uint32_t dev_class;
    char compatible[32];
    size_t compat_len;
} reg_entry_t;

static reg_entry_t reg_table[MAX_DRIVERS];
static size_t reg_count;

static reg_entry_t *find_registration(uint32_t dev_class)
{
    for (size_t i = 0; i < reg_count; i++) {
        if (reg_table[i].dev_class == dev_class) {
            return &reg_table[i];
        }
    }
    return NULL;
}

static int request_and_grant_class(uint32_t dev_class, int32_t target_pid, uint32_t *out_granted)
{
    reg_entry_t *entry = find_registration(dev_class);
    if (!entry) {
        return ERR_NOENT;
    }

    int32_t dev_handle = _getdev(entry->compatible, entry->compat_len);
    if (dev_handle < 0) {
        return dev_handle;
    }

    int32_t granted_handle = _port_grant(dev_handle, target_pid);
    if (granted_handle < 0) {
        return granted_handle;
    }

    if (out_granted) {
        *out_granted = (uint32_t)granted_handle;
    }
    return 0;
}

static void handle_register(uint32_t sender, uint32_t dev_class, int32_t compat_shm_handle)
{
    if (find_registration(dev_class)) {
        _detach(compat_shm_handle);
        _reply(sender, DEV_REG_DUP, 0, 0);
        return;
    }

    if (reg_count >= MAX_DRIVERS) {
        _detach(compat_shm_handle);
        _reply(sender, DEV_REG_FULL, 0, 0);
        return;
    }

    char *compat = (char *)_attach(compat_shm_handle);
    if ((intptr_t)compat <= 0) {
        _detach(compat_shm_handle);
        _reply(sender, ERR_BADARG, 0, 0);
        return;
    }

    size_t len = strnlen(compat, sizeof(reg_table[0].compatible));
    if (len == 0 || len >= sizeof(reg_table[0].compatible)) {
        _detach(compat_shm_handle);
        _reply(sender, ERR_BADARG, 0, 0);
        return;
    }

    reg_table[reg_count].dev_class = dev_class;
    reg_table[reg_count].compat_len = len;
    memmove(reg_table[reg_count].compatible, compat, len + 1);
    reg_count++;

    _detach(compat_shm_handle);
    _reply(sender, DEV_REG_OK, 0, 0);
}

static void handle_dev_request(uint32_t sender, uint32_t dev_class)
{
    uint32_t granted_serial = 0;
    int rc = request_and_grant_class(dev_class, (int32_t)sender, &granted_serial);
    if (rc < 0) {
        _reply(sender, (uint32_t)rc, 0, 0);
        return;
    }

    _reply(sender, 0, granted_serial, 0);
}

int devmgr_setup(void)
{
    int32_t my_port = _port_create();
    if (my_port < 0) {
        return my_port;
    }

    int32_t nt_slot = _port_grant(my_port, NAMETABLE_PID);
    if (nt_slot < 0) {
        return nt_slot;
    }

    zuzu_ipcmsg_t reg = _call(NT_PORT, NT_REGISTER, nt_pack(DEVMGR_NAME), (uint32_t)nt_slot);
    if ((int32_t)reg.r1 != NT_REG_OK) {
        return ERR_BUSY;
    }

    return my_port;
}

void devmgr_loop(int32_t port_handle)
{
    while (1) {
        zuzu_ipcmsg_t msg = _recv(port_handle);

        if (msg.r1 == DEV_REGISTER) {
            handle_register((uint32_t)msg.r0, msg.r2, (int32_t)msg.r3);
        } else if (msg.r1 == DEV_REQUEST) {
            handle_dev_request((uint32_t)msg.r0, msg.r2);
        } else {
            _reply((uint32_t)msg.r0, ERR_BADCMD, 0, 0);
        }
    }
}

int main(void)
{
    int32_t port = devmgr_setup();
    if (port < 0) {
        return 1;
    }

    devmgr_loop(port);
    return 0;
}