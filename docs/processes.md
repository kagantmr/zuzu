# Zuzu Process Model

This document describes how processes are represented, created, scheduled, and destroyed in Zuzu.

---

## Process Control Block (`kernel/proc/process.h`)

Every process is represented by a `process_t` struct:

```c
typedef struct process {
    uint32_t         pid;               // unique process ID
    uint32_t         parent_pid;        // PID of parent (0 = kernel)
    p_state_t        process_state;     // READY / RUNNING / BLOCKED / ZOMBIE
    uint32_t        *kernel_sp;         // current kernel stack pointer (for context switch)
    uintptr_t        kernel_stack_top;  // base of kernel stack allocation (for freeing)
    uint32_t         wake_tick;         // tick at which a sleeping process should wake
    uint32_t         priority;          // scheduling priority (future use)
    uint32_t         time_slice;        // ticks per scheduling quantum
    uint32_t         ticks_remaining;   // ticks remaining in current quantum
    addrspace_t     *as;                // this process's address space (TTBR0 root)
    list_node_t      node;              // embedded list node (run queue / endpoint queue)
    int32_t          exit_status;       // exit code, valid when state == ZOMBIE
    exception_frame_t *trap_frame;      // saved register state (points into kernel stack)
    endpoint_t      *handle_table[16];  // IPC handle table (index 0 = name server)
    ipc_state_t      ipc_state;         // IPC_NONE / IPC_SENDER / IPC_RECEIVER
    endpoint_t      *blocked_endpoint;  // endpoint this process is waiting on (if any)
} process_t;
```

---

## Process States

```
         process_create()
               │
               ▼
           ┌───────┐
           │ READY │ ◄──────────────────────────────┐
           └───┬───┘                                │
               │ scheduler picks it                 │
               ▼                                    │
          ┌─────────┐    tick / yield / IPC    ┌────┴───┐
          │ RUNNING │ ──────────────────────► │  READY │
          └────┬────┘                          └────────┘
               │
        ┌──────┴──────┐
        │             │
   IPC blocks     task_quit
        │             │
        ▼             ▼
   ┌─────────┐   ┌────────┐
   │ BLOCKED │   │ ZOMBIE │
   └────┬────┘   └────────┘
        │
   IPC completes
        │
        ▼
     READY (re-added to run queue)
```

<!-- TODO: explain ZOMBIE state — process has exited but PCB is kept until parent calls task_wait. -->
<!-- If parent never calls task_wait, the zombie accumulates (init should reap orphans). -->

---

## Kernel Stacks

Each process has its own kernel stack, separate from its user stack. The kernel stack is used when the process is executing in kernel mode (during a syscall or IRQ that interrupted the process).

<!-- TODO: document kernel stack size and where it comes from (pmm alloc via kstack.c) -->
<!-- TODO: explain the guard page at the bottom of the kernel stack — an unmapped page that -->
<!-- causes a Data Abort on stack overflow instead of silent corruption. -->

The `kernel_stack_top` field records the physical base of the allocation so it can be freed when the process is destroyed. `kernel_sp` is the current stack pointer value — it moves as things are pushed/popped during exceptions.

---

## Context Switch (`arch/arm/exceptions/switch.s`)

Context switching in Zuzu works by saving and restoring the **callee-saved registers** (r4–r11 + lr) on the kernel stack. The scheduler calls `context_switch(prev, next)` which:

1. Pushes `{r4–r11, lr}` onto `prev`'s kernel stack. Saves `sp` into `prev->kernel_sp`.
2. Loads `sp` from `next->kernel_sp`. Pops `{r4–r11, lr}` from `next`'s kernel stack.
3. Returns — but "returns" into the `next` process's context (wherever it last called `context_switch`).

For a brand-new process that has never run before, the kernel stack is pre-initialized to look like the result of a context switch. The `lr` in the saved context points to `process_entry_trampoline`, which sets up the exception return frame that transitions to user mode.

<!-- TODO: explain process_entry_trampoline in more detail — it constructs the exception frame -->
<!-- with CPSR mode=USR and jumps to the user entry point via RFEIA. -->

---

## Address Spaces

Each process has an `addrspace_t *as` pointer. The address space contains:

- An L1 page table (16KB aligned, allocated from PMM)
- The set of `vm_region_t` mappings (code, stack, heap)

On a context switch, `TTBR0` is written with the physical address of the new process's L1 table. `TTBR1` (kernel mappings) never changes.

<!-- TODO: document as_create(), vmm_map_user(), as_destroy() -->
<!-- TODO: note the TLB invalidation that must happen after writing TTBR0 -->

---

## Process Creation (`kernel/proc/process.c`)

`process_create(entry, magic)` allocates and initializes a new process:

1. Allocates a `process_t` from the heap.
2. Allocates and zeroes a kernel stack (via `kstack_alloc()`).
3. Creates an address space (`as_create()`).
4. Pre-initializes the kernel stack with a fake context switch frame pointing to `process_entry_trampoline`.
5. Assigns a PID.
6. Sets state to `PROCESS_READY`.

The `magic` parameter is currently used in test processes to identify which test function the process should run.

<!-- TODO: replace the magic-based dispatch with proper ELF loading in Phase 13 -->

---

## Process Destruction

`process_destroy(p)` is called when a ZOMBIE process is reaped:

1. Frees the address space (`as_destroy()` — walks L2 tables, frees pages, frees L1).
2. Frees the kernel stack.
3. Frees the `process_t` struct itself.

<!-- TODO: document the zombie reaping path — where/when does this get called? -->
<!-- Currently: scheduler or idle loop? -->

---

## Scheduler (`kernel/sched/sched.c`)

Zuzu uses a simple round-robin scheduler. All ready processes sit in a linked list. On each tick, the scheduler picks the next process in the list, performs a context switch, and updates the current process pointer.

<!-- TODO: document sched_init(), sched_add(), sched_remove(), schedule() -->
<!-- TODO: explain current_process global and why it's important for IPC and syscall handling -->
<!-- TODO: note the tick_remaining / time_slice mechanism (preemption after N ticks) -->

The scheduler is triggered in two ways:
- **Timer tick** — `schedule()` is registered as a tick callback, called from the timer IRQ handler.
- **Voluntary** — IPC blocking syscalls and `task_yield` call `schedule()` directly.

---

## See Also

- `kernel/proc/process.c` — process_create, process_destroy
- `kernel/proc/kstack.c` — kernel stack allocation with guard pages
- `arch/arm/exceptions/switch.s` — context_switch assembly
- `kernel/sched/sched.c` — scheduler run queue and tick dispatch
- `kernel/vmm/vmm.c` — address space management
- [ipc.md](ipc.md) — how blocking IPC interacts with process states
- [arch.md](arch.md) — TTBR0 switching, USR mode entry