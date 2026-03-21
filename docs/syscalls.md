# zuzu Syscall ABI

## Encoding

Syscall numbers are encoded in the lower 8 bits of the SVC immediate field.
Numbers are grouped by category with room for expansion within each group.

- ARM mode: `SVC #n` — kernel reads 32-bit instruction, masks with `& 0xFF`
- Thumb mode (future): same mask, 16-bit instruction read

## Calling Convention

- **Arguments**: r0–r3
- **Return value**: r0
- **Preserved**: r1–r12, sp, lr
- **Clobbered**: r0 only

## Error Codes

Negative r0 values indicate errors:

| Value | Name         | Meaning                  |
| ----- | ------------ | ------------------------ |
| -1    | ERR_NOPERM   | Operation not permitted  |
| -2    | ERR_NOENT    | Not found                |
| -3    | ERR_BUSY     | Resource busy, try again |
| -4    | ERR_NOMEM    | Out of memory            |
| -5    | ERR_BADFORM  | Bad handle / descriptor  |
| -6    | ERR_BADARG   | Invalid argument         |
| -7    | ERR_NOMATCH  | Syscall not implemented  |
| -8    | ERR_PTRFAULT | Bad pointer              |

## 0x00–0x0F: Task Lifecycle

| #    | Name       | Arguments                  | Returns       | Description                          |
| ---- | ---------- | -------------------------- | ------------- | ------------------------------------ |
| 0x00 | task_quit  | r0: status                 | never returns | Terminate calling task, store status |
| 0x01 | task_yield | —                          | 0             | Voluntarily reschedule               |
| 0x02 | task_spawn | r0: name ptr, r1: name len | pid or err    | Create new task from initrd ELF      |
| 0x03 | task_wait  | r0: pid, r1: &status       | 0 or err      | Block until child exits              |
| 0x04 | get_pid    | —                          | pid           | Return caller's PID                  |
| 0x05 | task_sleep | r0: duration (ms)          | 0             | Sleep for given duration             |

## 0x10–0x1F: IPC

| #    | Name       | Arguments                | Returns                                | Description                        |
| ---- | ---------- | ------------------------ | -------------------------------------- | ---------------------------------- |
| 0x10 | proc_send  | r0: port, r1–r3: payload | 0 or err                               | Send message, block until received |
| 0x11 | proc_recv  | r0: port                 | IRQ: r0=0,r1=irq; send: r0=pid,r1-r3; call: r0=reply_handle,r1=pid,r2-r3 | Receive message/event              |
| 0x12 | proc_call  | r0: port, r1–r3: payload | r0–r3: reply                           | Send then block for reply (RPC)    |
| 0x13 | proc_reply | r0: reply_handle, r1–r3: payload | 0 or err                    | Reply via reply capability handle  |

## 0x20–0x2F: Ports

| #    | Name         | Arguments           | Returns        | Description                      |
| ---- | ------------ | ------------------- | -------------- | -------------------------------- |
| 0x20 | port_create  | —                   | handle or err  | Create new IPC port              |
| 0x21 | port_destroy | r0: handle          | 0 or err       | Destroy a port                   |
| 0x22 | port_grant   | r0: handle, r1: pid | slot_id or err | Give port access to another task |

## 0x30–0x3F: Memory/Devices

| #    | Name     | Arguments                    | Returns               | Description                       |
| ---- | -------- | ---------------------------- | --------------------- | --------------------------------- |
| 0x30 | memmap   | r0: addr, r1: size, r2: prot | addr or err           | Map pages into caller's space     |
| 0x31 | memunmap | r0: addr, r1: size           | 0 or err              | Unmap and free pages              |
| 0x32 | memshare | r0: size                     | r0:handle, r1:addr or err | Create and map shared memory  |
| 0x33 | attach   | r0: handle                   | addr or err           | Map shared object into caller     |
| 0x34 | mapdev   | r0: device handle            | addr or err           | Map MMIO region from device cap   |
| 0x35 | detach   | r0: shmem handle             | 0 or err              | Detach shared memory              |

## 0x40–0x4F: Interrupts

| #    | Name      | Arguments                         | Returns  | Description                      |
| ---- | --------- | --------------------------------- | -------- | -------------------------------- |
| 0x40 | irq_claim | r0: device handle                | 0 or err | Claim IRQ described by device cap |
| 0x41 | irq_bind  | r0: device handle, r1: port      | 0 or err | Bind IRQ delivery to an IPC port |
| 0x42 | irq_done  | r0: device handle                | 0 or err | Acknowledge and unmask IRQ line  |


## 0xF0–0xFF: Experimental/Debug (temporary)

| #    | Name        | Arguments            | Returns  | Description                        |
| ---- | ----------- | -------------------- | -------- | ---------------------------------- |
| 0xF0 | log         | r0: msg ptr, r1: len | 0 or err | Print string to kernel console     |
| 0xF1 | dump        | —                    | 0        | Dump caller's register state       |
| 0xF2 | pmm_getfree | —                    | pages    | Return current free PMM page count |

---

## Name Service

The name service is a userspace server reachable via a well-known port.
The kernel pre-populates port handle 0 in every task's handle table to point to the name server's endpoint.

Tasks interact with it via normal IPC:

- `proc_call(0, REGISTER, "uart", 4)` — register a service name
- `proc_call(0, LOOKUP, "uart", 4)` — look up a service, receive port handle via grant

---

## Notes

- All pointer arguments must point to user memory (below 0xC0000000). The kernel validates before dereferencing.
- `mapdev` maps by device handle, not raw physical ranges.
- `proc_recv` is multiplexed: IRQ events and IPC messages share one return tuple.
- `proc_call` = atomic `proc_send` + `proc_recv` on the same port.
- `proc_reply` takes a reply-handle in r0 and payload in r1-r3.