#include "devmgr.h"
#include <string.h>
#include <mem.h>

#define MAX_DRIVERS 64
#define MAX_HANDLE_SCAN 256

typedef struct {
    uint32_t dev_class;
    uint32_t injected_handle;
} reg_entry_t;

static reg_entry_t reg_table[MAX_DRIVERS];
static size_t reg_count;

static const char *class_to_compat(uint32_t dev_class)
{
    switch (dev_class) {
    case DEV_CLASS_SERIAL:
        return "arm,pl011";
    case DEV_CLASS_RTC:
        return "arm,pl031";
    case DEV_CLASS_BLOCK:
        return "arm,pl180";
    default:
        return NULL;
    }
}

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

    int32_t granted_handle = _port_grant((int32_t)entry->injected_handle, target_pid);
    if (granted_handle < 0) {
        return granted_handle;
    }

    if (out_granted) {
        *out_granted = (uint32_t)granted_handle;
    }
    return 0;
}

static void build_class_table(void) {
    char compat[32];
    reg_count = 0;

    int32_t serial_handle = -1;
    int32_t serial_irq = -1;
    int32_t rtc_handle = -1;
    int32_t rtc_irq = -1;
    int32_t block_handle = -1;
    int32_t block_irq = -1;

    for (uint32_t i = 1; i < MAX_HANDLE_SCAN && reg_count < MAX_DRIVERS; i++) {
        int32_t rc = _querydev(i, compat, sizeof(compat));
        if (rc < 0)
            continue;

        if (strncmp(compat, "arm,pl011", 9) == 0) {
            /* Prefer a UART entry with a real IRQ; IRQ 0 devices are typically unusable for RX interrupts. */
            if (serial_handle < 0 || (serial_irq <= 0 && rc > 0)) {
                serial_handle = (int32_t)i;
                serial_irq = rc;
            }
            continue;
        }

        if (strncmp(compat, "arm,pl031", 9) == 0) {
            if (rtc_handle < 0 || (rtc_irq <= 0 && rc > 0)) {
                rtc_handle = (int32_t)i;
                rtc_irq = rc;
            }
            continue;
        }

        if (strncmp(compat, "arm,pl180", 9) == 0) {
            if (block_handle < 0 || (block_irq <= 0 && rc > 0)) {
                block_handle = (int32_t)i;
                block_irq = rc;
            }
            continue;
        }

    }

    if (serial_handle > 0 && reg_count < MAX_DRIVERS) {
        reg_table[reg_count++] = (reg_entry_t){ DEV_CLASS_SERIAL, (uint32_t)serial_handle };
    }
    if (rtc_handle > 0 && reg_count < MAX_DRIVERS) {
        reg_table[reg_count++] = (reg_entry_t){ DEV_CLASS_RTC, (uint32_t)rtc_handle };
    }
    if (block_handle > 0 && reg_count < MAX_DRIVERS) {
        reg_table[reg_count++] = (reg_entry_t){ DEV_CLASS_BLOCK, (uint32_t)block_handle };
    }
}

static void handle_register(uint32_t reply_handle, uint32_t dev_class)
{
    (void)dev_class;
    _reply(reply_handle, DEV_REG_OK, 0, 0);
}

static void handle_dev_request(uint32_t reply_handle, uint32_t sender_pid, uint32_t dev_class)
{
    const char *compat = class_to_compat(dev_class);
    if (!compat) {
        _reply(reply_handle, ERR_BADARG, 0, 0);
        return;
    }

    uint32_t granted_serial = 0;
    int rc = request_and_grant_class(dev_class, (int32_t)sender_pid, &granted_serial);
    if (rc < 0) {
        _reply(reply_handle, (uint32_t)rc, 0, 0);
        return;
    }

    _reply(reply_handle, 0, granted_serial, 0);
}

int devmgr_setup(void)
{
    build_class_table();
    int32_t my_port = _port_create();
    if (my_port < 0) {
        return my_port;
    }

    int32_t nt_slot = _port_grant(my_port, NAMETABLE_PID);
    if (nt_slot < 0) {
        return nt_slot;
    }

    (void)_send(NT_PORT, NT_REGISTER, nt_pack(DEVMGR_NAME), (uint32_t)nt_slot);

    return my_port;
}

void devmgr_loop(int32_t port_handle)
{
    while (1) {
        zuzu_ipcmsg_t msg = _recv(port_handle);

        uint32_t reply_handle = (uint32_t)msg.r0;
        uint32_t sender_pid = msg.r1;
        uint32_t command = msg.r2;
        uint32_t arg = msg.r3;

        if (command == DEV_REGISTER) {
            handle_register(reply_handle, arg);
        } else if (command == DEV_REQUEST) {
            handle_dev_request(reply_handle, sender_pid, arg);
        } else {
            _reply(reply_handle, ERR_BADCMD, 0, 0);
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