# zuzu Syscall ABI
This document describes the kernel syscall interface exposed through `SVC #imm8`.

---
## Encoding

Syscall numbers are encoded in the low 8 bits of the SVC immediate.
- ARM mode: the kernel reads the 32-bit instruction and masks with `0xFF`.
- Thumb mode: the kernel reads the 16-bit instruction and masks with `0xFF`.

Both modes use the same 8-bit syscall number space. The exception dispatcher checks CPSR bit 5 (Thumb flag) to determine instruction width and reads accordingly.

---
## Calling Convention

- Arguments: `r0`-`r3`
- Return values: `r0`, or `r0`-`r3` for multi-value calls
- Treat `r0`-`r3` as volatile across a syscall
Syscalls return through the exception frame, so the kernel does not expose a C ABI. Some calls write additional results into `r1`-`r3`.

---
## Error Codes

Negative `r0` values indicate errors.
| Value | Name         | Meaning                        |
| ----- | ------------ | ------------------------------ |
| -1    | ERR_NOPERM   | Operation not permitted        |
| -2    | ERR_NOENT    | Not found                      |
| -3    | ERR_BUSY     | Resource busy, try again       |
| -4    | ERR_NOMEM    | Out of memory                  |
| -5    | ERR_BADFORM  | Bad handle type / descriptor   |
| -6    | ERR_BADARG   | Invalid argument               |
| -7    | ERR_NOMATCH  | Syscall not implemented        |
| -8    | ERR_PTRFAULT | Bad pointer                    |
| -9    | ERR_DEAD     | Object destroyed while waiting |

---
## Syscall Reference Table

All syscalls are invoked via `SVC #N` where `N` is the syscall number in the table below:

| #    | Name           | Category      | Arguments                                     | Returns                  |
| ---- | -------------- | ------------- | --------------------------------------------- | ------------------------ |
| 0x00 | task_quit      | Task          | `r0: status`                                  | never returns            |
| 0x01 | task_yield     | Task          | none                                          | `r0 = 0`                 |
| 0x03 | task_wait      | Task          | `r0: pid`, `r1: status*`, `r2: flags`        | child pid or error       |
| 0x04 | get_pid        | Task          | none                                          | current pid              |
| 0x05 | task_sleep     | Task          | `r0: milliseconds`                            | `r0 = 0`                 |
| 0x06 | task_tspawn    | Task          | `r0: name_ptr`                                | `r0: handle`, `r1: pid`  |
| 0x07 | task_kickstart | Task          | `r0: kickstart_args_t*`                       | `r0 = 0` or error        |
| 0x08 | task_kill      | Task          | `r0: task handle`                             | `r0 = 0` or error        |
| 0x10 | proc_send      | IPC           | `r0: port handle`, `r1-r3: payload`          | `r0 = 0` or error        |
| 0x11 | proc_recv      | IPC           | `r0: port handle`, `r1: timeout_ms`          | sender info or error     |
| 0x12 | proc_call      | IPC           | `r0: port handle`, `r1-r3: payload`          | later resumes w/ reply   |
| 0x13 | proc_reply     | IPC           | `r0: reply handle`, `r1-r3: payload`         | `r0 = 0` or error        |
| 0x14 | proc_sendx     | IPC           | `r0: port handle`, `r1: length`               | `r0 = 0` or error        |
| 0x15 | proc_callx     | IPC           | `r0: port handle`, `r1: length`               | later resumes w/ reply   |
| 0x16 | proc_replyx    | IPC           | `r0: reply handle`, `r1: length`              | `r0 = 0` or error        |
| 0x20 | port_create    | Ports         | none                                          | handle or error          |
| 0x21 | port_destroy   | Ports         | `r0: handle`                                  | `r0 = 0` or error        |
| 0x22 | port_grant     | Ports         | `r0: handle`, `r1: pid`                       | `r0 = 0` or error        |
| 0x23 | ntfn_create    | Notifications | none                                          | handle or error          |
| 0x24 | ntfn_signal    | Notifications | `r0: handle`, `r1: bits`                      | `r0 = 0` or error        |
| 0x25 | ntfn_wait      | Notifications | `r0: handle`                                  | bits or error            |
| 0x26 | ntfn_poll      | Notifications | `r0: handle`                                  | bits or error            |
| 0x30 | memmap         | Memory        | `r0: addr_hint`, `r1: size`, `r2: prot`      | mapped address or error  |
| 0x31 | memunmap       | Memory        | `r0: addr`, `r1: size`                        | `r0 = 0` or error        |
| 0x32 | memshare       | Memory        | `r0: size`                                    | `r0: handle`, `r1: addr` |
| 0x33 | attach         | Memory        | `r0: shmem handle`                            | mapped address or error  |
| 0x34 | mapdev         | Memory        | `r0: device handle`                           | mapped address or error  |
| 0x35 | detach         | Memory        | `r0: shmem handle`                            | `r0 = 0` or error        |
| 0x36 | querydev       | Memory        | `r0: device handle`, `r1: out_buf`, `r2: len` | `r0 = irq` or error      |
| 0x37 | mprotect       | Memory        | `r0: addr`, `r1: size`, `r2: prot`            | `r0 = 0` or error        |
| 0x38 | asinject       | Memory        | `r0: asinject_args_t*`                        | `r0 = 0` or error        |
| 0x40 | irq_claim      | Interrupts    | `r0: device handle`                           | `r0 = 0` or error        |
| 0x41 | irq_bind       | Interrupts    | `r0: device handle`, `r1: ntfn handle`        | `r0 = 0` or error        |
| 0x42 | irq_done       | Interrupts    | `r0: device handle`                           | `r0 = 0` or error        |

---

---
## Notes

- All pointer arguments must point to user memory below `USER_VA_TOP` (`0x80000000`).
- Handle types matter: passing a handle of the wrong type returns `ERR_BADFORM` or `ERR_NOPERM` depending on the syscall.
- `proc_recv`, `proc_call`, and the `*x` variants may block and therefore interact with the scheduler.
- `proc_reply` and `proc_replyx` use reply handles, not port handles.
- `mapdev` and `querydev` operate on device capabilities, not raw physical addresses.
