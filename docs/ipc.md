# Zuzu IPC

Inter-process communication is the defining feature of a microkernel. In Zuzu, processes cannot call kernel functions to access services — they send messages to server processes that own those services. The kernel's job in IPC is to be a fast, correct switchboard.

---

## Model: Synchronous Rendezvous

Zuzu uses **synchronous blocking IPC**. This means:

- `send` blocks the caller until a receiver is ready on the endpoint.
- `recv` blocks the caller until a sender arrives.
- When both are present, the kernel copies the message and unblocks both in one operation.
- There are no message queues. There is no heap allocation in the IPC hot path.

This is the same model used by L4, seL4, and QNX. The reason for choosing it over asynchronous IPC is simplicity: no buffer management, no flow control, no partial delivery. The trade-off is that a slow server blocks its clients. For the current stage of Zuzu this is fine — proper protocol design handles it at the application level.

---

## Primitives

### `proc_send` — `SVC #0x10`

```
r0: handle (index into calling process's handle table)
r1–r3: message payload (up to 3 words)
returns r0: 0 on success, negative error on failure
```

If a receiver is blocked on the target endpoint, the message is copied into the receiver's saved register frame and both processes are made runnable. If no receiver is waiting, the sender is added to the endpoint's sender queue and `schedule()` is called — the sender does not return until a receiver collects it.

### `proc_recv` — `SVC #0x11`

```
r0: handle
returns r0: sender's PID
         r1–r3: message payload
```

If a sender is already queued on the endpoint, it is dequeued, its payload is copied into the receiver's frame, and both run. If no sender is waiting, the receiver blocks.

### `proc_call` — `SVC #0x12`

```
r0: handle
r1–r3: message payload
returns r0–r3: reply payload
```

Atomic send-then-wait-for-reply. Equivalent to `proc_send` followed by immediately blocking for a reply from the same port. Used for request/response patterns (most IPC is request/response). The receiver uses `proc_reply` to unblock the caller.

### `proc_reply` — `SVC #0x13`

```
r0–r3: reply payload
returns: 0 or error
```

Sends a reply to whoever last called `proc_call` on the current process. Does not take a handle — the kernel knows who is waiting for a reply because it tracked the caller on `proc_call`.

<!-- TODO: document how the kernel tracks the pending caller for proc_reply — is this stored in the PCB? -->

---

## Endpoints and Ports

<!-- TODO: explain the distinction between endpoints and ports in Zuzu's model: -->
<!-- An endpoint (kernel/ipc/endpoint.h) is the kernel object — it has a sender queue and a receiver queue. -->
<!-- A port (kernel/ipc/sys_port.h) is... (explain what sys_port.c does vs sys_ipc.c) -->
<!-- Currently both exist. Clarify which one user processes interact with and which is internal. -->

**`endpoint_t` structure (`kernel/ipc/endpoint.h`):**

```c
typedef struct endpoint {
    list_head_t sender_queue;    // processes blocked on proc_send
    list_head_t receiver_queue;  // processes blocked on proc_recv
    uint32_t    owner_pid;
    list_node_t node;
} endpoint_t;
```

---

## Handle Tables

Processes do not hold raw kernel pointers to endpoints. Instead, each process has a **handle table** — a fixed-size array of 16 slots in its PCB:

```c
endpoint_t *handle_table[MAX_HANDLE_TABLE];  // MAX_HANDLE_TABLE = 16
```

Syscalls take a handle index (0–15), not a pointer. The kernel looks up `current_process->handle_table[handle]` and validates it before touching the endpoint.

This prevents processes from forging references to endpoints they were never given access to. A process can only reach an endpoint if the kernel placed it in the process's table.

**Handle 0** is reserved: the kernel pre-populates it in every process with a pointer to the name server's endpoint, so any process can look up services without prior coordination.

<!-- TODO: handle 0 may not be implemented yet — mark it as planned if so -->

---

## Message Format

At present, a message is exactly four 32-bit words: `r0–r3`. This maps directly to the ARM calling convention's argument registers, so there is no translation cost — the sender's registers ARE the message.

No heap allocation occurs. The kernel copies four words from one saved register frame to another.

<!-- TODO: note the planned extension: shared memory (Phase 19) for large payloads. The message then -->
<!-- carries a handle to a shared memory object, not the data itself. -->

---

## IPC and the Scheduler

Blocking IPC (`proc_send` with no receiver, or `proc_recv` with no sender) directly calls `schedule()`. The blocked process is removed from the run queue and placed on the endpoint's wait queue. When the matching party arrives, the IPC completion code calls `sched_add()` to put the woken process back on the run queue.

This means IPC is a first-class scheduling event — not a spin-wait or a poll. A process that is waiting for a message consumes no CPU.

---

## Error Cases

| Condition | Return value |
|-----------|-------------|
| Handle out of range (≥ 16) | `ERR_BADARG` (-6) |
| Handle slot is NULL | `ERR_BADARG` (-6) |
| No current process | `ERR_BADARG` (-6) |
| Endpoint destroyed while waiting | `ERR_DEAD` (-9) |

<!-- TODO: document the invalid handle test (Test D from the roadmap) and what the kernel actually returns -->

---

## Creating Endpoints

```
port_create — SVC #0x20
returns: handle index, or negative error
```

Allocates an `endpoint_t` via `kmalloc`, initializes the queues, assigns a slot in the calling process's handle table, and returns the slot index.

```
port_destroy — SVC #0x21
r0: handle
```

<!-- TODO: what happens to processes blocked on a destroyed endpoint? They should get ERR_DEAD. -->
<!-- Document whether this is implemented or planned. -->

---

## Port Grants

```
port_grant — SVC #0x22
r0: handle (in caller's table)
r1: target PID
```

Copies the endpoint reference from the caller's handle table into the target process's handle table. This is the mechanism for sharing endpoints — the kernel atomically manipulates two handle tables.

<!-- TODO: This is the hardest part of the name service (Phase 18). Note current status. -->

---

## See Also

- `kernel/ipc/endpoint.h` — endpoint structure
- `kernel/ipc/sys_ipc.c` — proc_send / proc_recv / proc_call / proc_reply implementation
- `kernel/ipc/sys_port.c` — port_create / port_destroy / port_grant
- `kernel/proc/process.h` — PCB, handle table, IPC state fields
- [syscalls.md](syscalls.md) — full ABI for IPC syscalls (0x10–0x22)
- [processes.md](processes.md) — blocking, unblocking, scheduler integration