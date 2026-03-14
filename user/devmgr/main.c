#include "devmgr.h"

#define LOG_LIT(s) _log((s), sizeof(s) - 1)

typedef struct {
    uint32_t id;
    const char *compatible;
    size_t compat_len;
} class_entry_t;

enum {
    DEVMGR_DEVICE_SERIAL = 1,
};

static const class_entry_t class_table[] = {
    { DEVMGR_DEVICE_SERIAL, "arm,pl011", 9 },
};

#define CLASS_TABLE_LEN (sizeof(class_table) / sizeof(class_table[0]))

static int resolve_compatible(uint32_t device_id, const char **out_compatible, size_t *out_len)
{
    for (size_t i = 0; i < CLASS_TABLE_LEN; i++) {
        if (class_table[i].id == device_id) {
            *out_compatible = class_table[i].compatible;
            *out_len = class_table[i].compat_len;
            return 0;
        }
    }
    return ERR_NOENT;
}

static int request_and_grant_class(uint32_t device_id, int32_t target_pid, uint32_t *out_granted)
{
    const char *compatible = NULL;
    size_t compat_len = 0;
    int rc = resolve_compatible(device_id, &compatible, &compat_len);
    if (rc < 0) {
        return rc;
    }

    int32_t dev_handle = _getdev(compatible, compat_len);
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

static int wait_for_service(const char *name4, uint32_t *out_port, uint32_t *out_pid)
{
    while (1) {
        zuzu_ipcmsg_t ntmsg = _call(NT_PORT, NT_LOOKUP, nt_pack(name4), 0);
        if ((int32_t)ntmsg.r1 == NT_LU_OK) {
            if (out_port) {
                *out_port = ntmsg.r2;
            }
            if (out_pid) {
                *out_pid = ntmsg.r3;
            }
            return 0;
        }
        _sleep(10);
    }
}

static int spawn_and_bootstrap_zuart(void)
{
    int32_t pid = _spawn("bin/zuart", 9);
    if (pid < 0) {
        return pid;
    }

    uint32_t zuart_port = 0;
    uint32_t zuart_pid = 0;
    wait_for_service("uart", &zuart_port, &zuart_pid);

    uint32_t granted_serial = 0;
    int rc = request_and_grant_class(DEVMGR_DEVICE_SERIAL, (int32_t)zuart_pid, &granted_serial);
    if (rc < 0) {
        return rc;
    }

    zuzu_ipcmsg_t boot = _call((int32_t)zuart_port, ZUART_CMD_BOOTSTRAP, granted_serial, 0);
    if ((int32_t)boot.r1 != ZUART_SEND_OK) {
        return ERR_BUSY;
    }

    return 0;
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

    int bootstrap_rc = spawn_and_bootstrap_zuart();
    if (bootstrap_rc < 0) {
        LOG_LIT("devmgr: failed to spawn/bootstrap zuart\n");
        return bootstrap_rc;
    }

    return my_port;
}

void devmgr_loop(int32_t port_handle)
{
    while (1) {
        zuzu_ipcmsg_t msg = _recv(port_handle);
        _reply((uint32_t)msg.r0, ERR_BADARG, 0, 0);
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