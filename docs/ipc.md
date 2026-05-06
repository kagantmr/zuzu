# zuzu IPC

Inter-process communication is the defining feature of a microkernel. In zuzu, processes reach services through capability handles, not raw kernel pointers. The kernel's job in IPC is to be a fast, correct switchboard.

---

## Model: Synchronous Rendezvous

zuzu uses **synchronous blocking IPC**. This means:

- `send` blocks the caller until a receiver is ready on the endpoint.
- `recv` blocks the caller until a sender arrives.
- When both are present, the kernel copies the message and unblocks both in one operation.
- There are no message queues. There is no heap allocation in the IPC hot path.

The same rendezvous model is used for both the fixed 3-word IPC calls and the variable-length IPCX calls. The trade-off is that a slow server blocks its clients, so protocols need to be designed around that behavior.

---

## Primitives

### `proc_send` — `SVC #0x10`

`r0` is the endpoint handle and `r1`-`r3` are the 3-word payload. If a receiver is already waiting, the message is copied into that receiver's saved frame. Otherwise the sender blocks on the endpoint queue.

### `proc_recv` — `SVC #0x11`

`r0` is the endpoint handle and `r1` is the timeout in milliseconds. If a sender is ready, the receiver gets the sender PID in `r0` and the payload in `r1`-`r3`. If the send was a call, the receiver gets a reply handle in `r0` and the sender PID in `r1`.

### `proc_call` — `SVC #0x12`

`r0` is the endpoint handle and `r1`-`r3` are the request payload. The caller blocks until the callee replies. The receiver uses the reply handle returned by `proc_recv` to answer the call.

### `proc_reply` — `SVC #0x13`

`r0` is the reply-handle slot and `r1`-`r3` are the reply payload. Reply handles are temporary capabilities that are destroyed after use.


---

## Endpoints and Ports

An endpoint is a kernel object that represents a communication channel. It has one queue for senders waiting for a receiver and one queue for receivers waiting for a sender. Processes do not interact with endpoints directly; instead, they get capability handles to endpoints in their handle table.

**`endpoint_t` structure (`kernel/ipc/endpoint.h`):**

```c
typedef struct endpoint {
    list_head_t sender_queue;
    list_head_t receiver_queue;
    uint32_t owner_pid;
    uint32_t ref_count;
    bool alive;
    list_node_t node;
} endpoint_t;
```

`owner_pid` controls destruction, `ref_count` tracks how many handle slots reference the endpoint, and `alive` tells the kernel whether the endpoint has already been destroyed.

---

## Handle Tables

Processes do not hold raw kernel pointers to endpoints. Instead, each process has a typed capability vector in its PCB. Syscalls take a handle index, not a pointer, and the kernel validates the slot before touching the object.

Handle 0 is reserved for the name-table endpoint when one exists. `process_create()` populates it automatically once the name-table endpoint has been bootstrapped.

The current capability types are endpoint, device, shared memory, reply, notification, and task handles.

---

## IPCX

At present, fixed-size IPC messages use `r0`-`r3`. Variable-length IPC uses the IPCX buffer instead. The buffer lives at `0x7FFFA000` and is one page wide (`4096` bytes).

Every process gets the IPCX mapping automatically during creation. The kernel clamps all IPCX transfers to `IPCX_BUF_SIZE`, and the shared buffer is mapped read/write in user space.

The variable-length syscalls are:

- `proc_sendx` - send a buffer into the IPCX transfer page
- `proc_callx` - request/reply using the IPCX page
- `proc_replyx` - reply using the IPCX page

The receiver sees the payload length in the reply registers. No extra kernel heap allocation is needed.

## Shared Memory

Shared memory is also capability-based. `memshare()` allocates page-aligned backing pages, maps them into the caller, and returns both a handle and the mapped address. `attach()` maps an existing shared-memory object into another process, and `detach()` unmaps the exact region and decrements the refcount.

Shared-memory handles can be transferred with `port_grant()`. When a shared-memory handle is copied, the kernel increments the object's refcount and clears the receiver's `mapped_va` so it can be mapped into the receiver's own address space.

---

## IPC and the Scheduler

Blocking IPC (`proc_send` with no receiver, or `proc_recv` with no sender) directly calls `schedule()`. The blocked process is removed from the run queue and placed on the endpoint's wait queue. When the matching party arrives, the IPC completion code calls `sched_add()` to put the woken process back on the run queue.

This means IPC is a first-class scheduling event — not a spin-wait or a poll. A process that is waiting for a message consumes no CPU.

---

## Error Cases

| Condition                        | Return value      |
| -------------------------------- | ----------------- |
| Handle out of range (≥ 16)       | `ERR_BADARG` (-6) |
| Handle slot is NULL              | `ERR_BADARG` (-6) |
| No current process               | `ERR_BADARG` (-6) |
| Endpoint destroyed while waiting | `ERR_DEAD` (-9)   |


---

## Creating Endpoints

```
port_create — SVC #0x20
returns: handle index, or negative error
```

Allocates an `endpoint_t`, initializes the queues, assigns a slot in the calling process's handle table, and returns the slot index.

```
port_destroy — SVC #0x21
r0: handle
```

Frees the endpoint and clears the handle slot. If there are any processes blocked on the endpoint, they are unblocked with an `ERR_DEAD` error code.
---

## Port Grants

```
port_grant — SVC #0x22
r0: handle (in caller's table)
r1: target PID
```

Copies the handle from the caller's table into the target process's table. This is the mechanism for sharing endpoints and other grantable capabilities. The kernel updates object refcounts as needed and clears the `grantable` flag for non-init recipients.

Name-service code uses this to distribute service endpoints to client processes.

---

## See Also

- `kernel/ipc/endpoint.h` - endpoint and capability types
- `kernel/ipc/sys_ipc.c` - `proc_send` / `proc_recv` / `proc_call` / `proc_reply` and IPCX variants
- `kernel/ipc/sys_port.c` - `port_create` / `port_destroy` / `port_grant`
- `kernel/mm/sys_mm.c` - `memshare` / `attach` / `detach`
- `kernel/proc/process.h` - PCB, capability table, IPC state fields
- [syscalls.md](syscalls.md) - full ABI for IPC syscalls (0x10-0x38)
- [processes.md](processes.md) - blocking, unblocking, scheduler integration, and task bootstrap