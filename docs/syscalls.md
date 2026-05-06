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
## Task Lifecycle Details (0x00-0x08)

### `task_quit` - Exit the current task

Exit with an integer status code. Does not return; the task is destroyed and removed from the scheduler.

### `task_yield` - Voluntarily reschedule

Yield the CPU to another ready task. Returns `0` on success.

### `task_wait` - Wait for child task exit

Wait for a child process (by PID or any child if `pid = -1`) to exit and retrieve its exit status. Supports `WNOHANG` flag for non-blocking polls.

**Arguments:**
- `r0`: child PID or `-1` for any child
- `r1`: pointer to status output buffer
- `r2`: flags (e.g., `WNOHANG`)

**Returns:**
- Exited child PID
- `0` if no child has exited and `WNOHANG` set
- `ERR_*` on error

### `get_pid` - Return caller's PID

Returns the PID of the currently executing task.

### `task_sleep` - Sleep for milliseconds

Sleep for at least the specified duration. May wake early if interrupted. Returns `0` on success.

### `task_tspawn` - Create a stopped task

Create a new task with the given name string. The task is created in stopped state and must be prepared (code/stack injected via `asinject`) before being started with `task_kickstart`.

**Arguments:**
- `r0`: pointer to null-terminated name string

**Returns:**
- `r0`: task handle
- `r1`: assigned PID

### `task_kickstart` - Start a stopped task

Start a previously stopped task after its code and stack have been prepared.

**Arguments:**
- `r0`: pointer to `kickstart_args_t` struct (see struct definition below)

**Returns:**
- `0` on success
- Error code on failure

**`kickstart_args_t` definition** (from `include/spawn_args.h`):
```c
typedef struct {
	uint32_t task_handle;
	uintptr_t entry;           // entry point address
	uintptr_t sp;              // stack pointer value
	uint32_t r0_val;           // initial r0 value
	uint32_t r1_val;           // initial r1 value
} kickstart_args_t;
```

### `task_kill` - Destroy a task handle

Destroy a stopped task (must not be running). Deallocates its resources.

**Arguments:**
- `r0`: task handle

**Returns:**
- `0` on success
- Error code on failure

**Note:** There is no direct ELF loader syscall. Userspace task spawning uses the pattern: `task_tspawn` -> `asinject` (via init) -> `task_kickstart`.

---
## IPC Details (0x10-0x16)

All IPC endpoints are referenced by handle, not by raw pointer. Handles are opaque 32-bit values returned by port creation.

### `proc_send` - Send a 3-word message

Send a 3-word payload to a port and block until a receiver takes it.

**Arguments:**
- `r0`: port handle
- `r1-r3`: 3-word payload

**Returns:**
- `0` on success
- Error code on failure (e.g., port destroyed while waiting)

### `proc_recv` - Receive from a port

Block until a message is available on the port, then return sender information and the payload.

**Arguments:**
- `r0`: port handle
- `r1`: timeout in milliseconds (0 = wait forever, `UINT32_MAX` = poll immediately)

**Return forms:**

1. **Send message received:**
   - `r0 = sender PID`
   - `r1-r3 = payload`

2. **Call message received (caller waiting for reply):**
   - `r0 = reply handle` (used with `proc_reply`)
   - `r1 = sender PID`
   - `r2-r3 = payload`

3. **Timeout:**
   - `r0 = ERR_BUSY`

### `proc_call` - Atomic request/reply

Send a 3-word request and block until the receiver replies. The caller resumes with the reply.

**Arguments:**
- `r0`: port handle
- `r1-r3`: request payload

**Returns (after reply arrives):**
- `r0 = 0`
- `r1-r3 = reply payload`

### `proc_reply` - Reply to a pending call

Send a 3-word reply back to a blocked caller.

**Arguments:**
- `r0`: reply handle (obtained from `proc_recv` when receiving a call)
- `r1-r3`: reply payload

**Returns:**
- `0` on success
- Error code on failure (e.g., reply handle expired)

### `proc_sendx` - Variable-length send

Send variable-length data via the shared IPCX buffer.

**Shared Buffer:**
- Location: `0x7FFFA000` (user-accessible)
- Size: `4096` bytes (kernel clamps to this max)

**Arguments:**
- `r0`: port handle
- `r1`: length in bytes to send

**How it works:**
1. Write data into IPCX buffer at `0x7FFFA000`
2. Call `proc_sendx` with the length
3. Kernel copies from IPCX buffer and delivers to receiver

**Receiver return form:**
- `r0 = sender PID`
- `r1 = received length`
- `r2-r3 = 0`

The receiver reads the data from the same IPCX buffer after resuming.

### `proc_callx` - Variable-length request/reply

Send a variable-length request and receive a variable-length reply via the IPCX buffer.

**Arguments:**
- `r0`: port handle
- `r1`: request length in bytes

**How it works:**
1. Write request data into IPCX buffer
2. Call `proc_callx`
3. Caller blocks; receiver wakes with call message
4. Receiver writes reply into IPCX buffer
5. Receiver calls `proc_replyx` with reply length
6. Caller resumes with reply length in `r1` and data in IPCX buffer

**Receiver return form:**
- `r0 = reply handle`
- `r1 = sender PID`
- `r2 = received length`
- `r3 = 0`

**Caller resumes with:**
- `r0 = 0`
- `r1 = reply length`
- Data in IPCX buffer

### `proc_replyx` - Reply with variable-length data

Reply to a pending `proc_callx` using the IPCX buffer.

**Arguments:**
- `r0`: reply handle
- `r1`: reply length in bytes

**How it works:**
1. Write reply data into IPCX buffer
2. Call `proc_replyx` with the length
3. Kernel delivers data to the blocked caller

**Returns:**
- `0` on success
- Error code on failure

---
## Ports and Notifications (0x20-0x26)

### Ports (0x20-0x22)

**`port_create`** - Create a new IPC endpoint

Returns a handle to a new port. Processes must explicitly grant handle access via `port_grant`.

**Returns:**
- `r0 = handle` on success
- `r0 = error` on failure

**`port_destroy`** - Destroy a port

Destroy an endpoint handle. Any waiting receivers are woken with `ERR_DEAD`.

**Arguments:**
- `r0: handle`

**Returns:**
- `0` on success
- Error code on failure

**`port_grant`** - Grant handle to another process

Copy a grantable handle from the current process into another task's handle table.

**Arguments:**
- `r0: handle` (must be marked grantable)
- `r1: target PID`

**Returns:**
- `0` on success
- Error code on failure

### Notifications (0x23-0x26)

**`ntfn_create`** - Create a notification object

Create a new notification object with a 32-bit word.

**Returns:**
- `r0 = handle` on success
- `r0 = error` on failure

**`ntfn_signal`** - Signal a notification

Bitwise-OR the provided bits into the notification word and wake one waiting task.

**Arguments:**
- `r0: handle`
- `r1: bits` (OR'd into the word)

**Returns:**
- `0` on success
- Error code on failure

**`ntfn_wait`** - Wait for notification bits

Block until the notification word becomes non-zero, then return and clear the word.

**Arguments:**
- `r0: handle`

**Returns:**
- `r0 = the word value before clearing`
- Error code on failure (e.g., handle destroyed)

**`ntfn_poll`** - Poll notification without blocking

Return the notification word and clear it. Does not block if the word is zero.

**Arguments:**
- `r0: handle`

**Returns:**
- `r0 = the word value before clearing (may be 0)`
- Error code on failure

---
## Memory and Devices (0x30-0x38)

### `memmap` - Map anonymous memory

Allocate and map anonymous user memory (demand-paged).

**Arguments:**
- `r0: addr_hint` (0 for auto-placement)
- `r1: size` (must be page-aligned)
- `r2: prot` (protection flags: `VM_PROT_READ`, `VM_PROT_WRITE`, `VM_PROT_EXEC`)

**Constraints:**
- **W^X enforcement:** `(prot & VM_PROT_WRITE) && (prot & VM_PROT_EXEC)` is forbidden
- Size must be > 0 and page-aligned
- Maximum allocation: 32 MB per call
- Address must be below `USER_VA_TOP` (0x80000000)

**Returns:**
- `r0 = mapped address` on success
- `r0 = error` on failure

**Behavior:** Pages are allocated on-demand when faulted (lazy allocation).

### `memunmap` - Unmap a region

Unmap an exact anonymous or shared memory region. Fails if region boundaries don't match exactly.

**Arguments:**
- `r0: base address`
- `r1: size` (must match the original allocation exactly)

**Constraints:**
- Must be page-aligned
- Region must exist and match exactly (no partial unmaps)

**Returns:**
- `0` on success
- Error code on failure

**Cleanup:** Anonymous pages are freed back to the PMM. Shared pages are decremented (and freed if refcount reaches 0).

### `memshare` - Create shared memory

Allocate a shared-memory region and return both a handle and a mapped address.

**Arguments:**
- `r0: size` (must be page-aligned)

**Returns:**
- `r0 = handle` (used with `attach` to map into other processes)
- `r1 = mapped address in caller's space`

**Note:** The caller is the first task to have this region mapped. Other tasks use `attach` with the handle to map it.

### `attach` - Map existing shared memory

Map an already-allocated shared-memory region (created by `memshare` or received via handle grant) into the caller's address space.

**Arguments:**
- `r0: shmem handle`

**Returns:**
- `r0 = mapped address` on success
- `r0 = error` on failure

**Increment:** The refcount of the shared-memory object is incremented.

### `mapdev` - Map device capability

Map a device capability into the caller's device window (currently at `0xF0000000`).

**Arguments:**
- `r0: device handle` (obtained from init or capability transfer)

**Returns:**
- `r0 = mapped address` on success
- `r0 = error` on failure

### `detach` - Unmap shared memory

Unmap a shared-memory object and decrement its reference count.

**Arguments:**
- `r0: shmem handle`

**Returns:**
- `0` on success
- Error code on failure

**Cleanup:** When refcount reaches 0, pages are freed.

### `querydev` - Query device information

Extract device information (compatible string and IRQ number) from a device capability.

**Arguments:**
- `r0: device handle`
- `r1: output buffer pointer`
- `r2: buffer size`

**Returns:**
- `r0 = IRQ number` on success
- `r0 = error` on failure

**Behavior:** Kernel copies the device's compatible string into the provided buffer (clamped to buffer size) and returns the IRQ number.

### `mprotect` - Change memory protections

Change the protection bits on an existing memory region.

**Arguments:**
- `r0: base address`
- `r1: size` (must be page-aligned)
- `r2: new prot flags`

**Constraints:**
- The region must exist (previously allocated via `memmap` or `memshare`/`attach`)
- Size must be > 0 and page-aligned
- **W^X enforcement:** `(prot & VM_PROT_WRITE) && (prot & VM_PROT_EXEC)` is forbidden (returns `ERR_BADARG`)

**Returns:**
- `0` on success
- Error code on failure

**Example:** Make a region read+execute (e.g., for executable code):
```c
mprotect(code_base, code_size, VM_PROT_READ | VM_PROT_EXEC);
```

### `asinject` - Inject code/data into stopped task

Inject pages of code or data into a stopped task's address space. Restricted to init tasks.

**Arguments:**
- `r0: pointer to `asinject_args_t` struct

**`asinject_args_t` definition** (from `include/spawn_args.h`):
```c
typedef struct {
	uint32_t task_handle;        // target task handle
	uintptr_t dst_va;            // destination VA in target's space
	const void *src_buf;         // source buffer in caller's space
	size_t len;                  // length in bytes
	uint32_t prot;               // protection flags for injected pages
} asinject_args_t;
```

**Constraints:**
- Caller must have `PROC_FLAG_INIT` set (only init tasks can inject)
- Target task must be in stopped state
- Source buffer must be in caller's user space
- Destination VA must be page-aligned
- Length must be > 0 and within the user VA boundary
- **W^X enforcement:** applied to injected pages
- Code pages (with `VM_PROT_EXEC`) are cache-flushed

**Returns:**
- `0` on success
- Error code on failure

**Behavior:** The kernel allocates physical pages, copies data from source buffer, sets up page table entries, and flushes the icache for executable pages.

---
## Interrupts (0x40-0x42)

### `irq_claim` - Claim an interrupt

Claim ownership of an IRQ described by a device capability.

**Arguments:**
- `r0: device handle`

**Returns:**
- `0` on success
- Error code on failure

**Behavior:** After claiming, the task receives IRQ notifications. IRQs must be bound to a notification object before delivery.

### `irq_bind` - Bind IRQ to notification

Bind a claimed IRQ to a notification object. When the IRQ fires, the notification is signaled.

**Arguments:**
- `r0: device handle`
- `r1: notification handle`

**Returns:**
- `0` on success
- Error code on failure

**Behavior:** When the IRQ fires, the kernel ORs the IRQ number (as a bit) into the notification word.

### `irq_done` - Re-enable IRQ

Re-enable a previously handled IRQ line. Must be called after handling the IRQ.

**Arguments:**
- `r0: device handle`

**Returns:**
- `0` on success
- Error code on failure

**Behavior:** Acknowledges the IRQ and re-enables it for future firing.

---
## Notes

- All pointer arguments must point to user memory below `USER_VA_TOP` (`0x80000000`).
- Handle types matter: passing a handle of the wrong type returns `ERR_BADFORM` or `ERR_NOPERM` depending on the syscall.
- `proc_recv`, `proc_call`, and the `*x` variants may block and therefore interact with the scheduler.
- `proc_reply` and `proc_replyx` use reply handles, not port handles.
- `mapdev` and `querydev` operate on device capabilities, not raw physical addresses.
