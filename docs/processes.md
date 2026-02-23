# zuzu Process Model

This document describes how processes are represented, created, scheduled, and destroyed in zuzu.

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

![Statistics screen](docs/img/boot_stats.png)

- `READY` — process is ready to run, sitting in the scheduler's run queue
- `RUNNING` — process is currently executing on the CPU
- `BLOCKED` — process is waiting for an event (e.g., IPC, timer tick) and is not runnable
- `ZOMBIE` — process has exited but has not been reaped by its parent yet. Its PCB still exists so the parent can query its exit status, but it is not runnable and does not consume resources.

---

## Kernel Stacks

Each process has its own kernel stack, separate from its user stack. The kernel stack is used when the process is executing in kernel mode (during a syscall or IRQ that interrupted the process).

The kernel stack is allocated from the PMM as a contiguous chunk of pages. The `kernel_stack_top` field in the PCB records the physical base address of this allocation so it can be freed when the process is destroyed. The `kernel_sp` field is the current stack pointer value, which moves as things are pushed and popped during exceptions. If kernel_sp gets corrupted (e.g., due to a stack overflow), the next exception will likely cause a Data Abort when the kernel tries to use the corrupted stack pointer and cause a panic.

The `kernel_stack_top` field records the physical base of the allocation so it can be freed when the process is destroyed. `kernel_sp` is the current stack pointer value — it moves as things are pushed/popped during exceptions.

---

## Context Switch (`arch/arm/exceptions/switch.s`)

Context switching in zuzu works by saving and restoring the **callee-saved registers** (r4–r11 + lr) on the kernel stack. The scheduler calls `context_switch(prev, next)` which:

1. Pushes `{r4–r11, lr}` onto `prev`'s kernel stack. Saves `sp` into `prev->kernel_sp`.
2. Loads `sp` from `next->kernel_sp`. Pops `{r4–r11, lr}` from `next`'s kernel stack.
3. Returns — but "returns" into the `next` process's context (wherever it last called `context_switch`).

For a brand-new process that has never run before, the kernel stack is pre-initialized to look like the result of a context switch. The `lr` in the saved context points to `process_entry_trampoline`, which sets up the exception return frame that transitions to user mode.


Before the first context switch, the kernel stack is pre-initialized to look like the result of a context switch. The `lr` in the saved context points to `process_entry_trampoline`, which sets up the exception return frame that transitions to user mode. This allows the scheduler to "return" into the new process's context on the first run, even though it has never actually executed before. This is the one and only time a process can be entered without going through the normal exception entry path. It is also the only usage of the SYS mode.

```armasm
process_entry_trampoline:
    ldmia sp!, {r0-r12, lr}
    cps #0x1F              @ SYS mode (shares SP with USR)
    mov sp, lr                @ Set User SP from LR
    mov lr, #0                @ Clear LR (prevent return to nowhere)
    cps #0x13              @ back to SVC
    rfeia sp!
```

---

## Address Spaces

Each process has an `addrspace_t *as` pointer. The address space contains:

- An L1 page table (16KB aligned, allocated from PMM)
- The set of `vm_region_t` mappings (code, stack, heap)

On a context switch, `TTBR0` is written with the physical address of the new process's L1 table. `TTBR1` (kernel mappings) never changes.

Address spaces are processor specific and managed through VMM policy code. The functions `as_create()`, `vmm_map_user()`, and `as_destroy()` handle the "algorithm", then delegate to architecture-specific code for the actual page table manipulations.
the TLB must be invalidated (flushed) after writing TTBR0 to ensure the new address space is used. This is done in `arch/arm/mmu.c` after every context switch.

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

The magic dispatcher will be replaced by an ELF loader in Phase 13, which will read the entry point from the ELF header and set up the initial user stack with arguments and environment variables.

---

## Process Destruction

`process_destroy(p)` is called when a ZOMBIE process is reaped:

1. Frees the address space (`as_destroy()` — walks L2 tables, frees pages, frees L1).
2. Frees the kernel stack.
3. Frees the `process_t` struct itself.

The scheduler currently reaps (deallocates) zombie processes. Later, this will be done by the "init" process.

---

## Scheduler (`kernel/sched/sched.c`)

zuzu uses a simple round-robin scheduler. All ready processes sit in a linked list. On each tick, the scheduler picks the next process in the list, performs a context switch, and updates the current process pointer.

sched_init() initializes the scheduler's run queue. sched_add() adds a process to the end of the run queue. sched_remove() removes a process from the run queue (e.g., when it blocks or exits). schedule() performs a context switch to the next ready process.

sched_add() is called when a process is created or unblocked. sched_remove() is called when a process blocks (e.g., on IPC) or exits. schedule() is called on every timer tick and whenever a process voluntarily yields the CPU (e.g., via task_yield or blocking IPC).

sched_remove() is called when a process blocks (e.g., on IPC) or exits. schedule() is called on every timer tick and whenever a process voluntarily yields the CPU (e.g., via task_yield or blocking IPC).

schedule() is the main scheduling function. It picks the next ready process from the run queue and performs a context switch. It also updates the `current_process` global pointer to point to the new process.

current_process is a global pointer that always points to the currently running process. This is important because when an exception (syscall or IRQ) occurs, the kernel needs to know which process was interrupted in order to access its PCB, address space, and other state. The current_process pointer is updated on every context switch to reflect the new running process.

The tick_remaining mechanism is a simple way to implement preemption. Each process has a time_slice (number of ticks it is allowed to run before being preempted) and ticks_remaining (number of ticks left in the current time slice). On each timer tick, the scheduler decrements ticks_remaining for the current process. If ticks_remaining reaches 0, the scheduler will preempt the process and switch to the next one in the run queue. When a process is scheduled, its ticks_remaining is reset to its time_slice.

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