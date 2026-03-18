# zuzu Architecture Document

This document covers the key ARM-specific design decisions in zuzu: why they were made, what the alternatives were, and what constraints they impose going forward.

---

## ARM Processor Modes

ARMv7-A has seven processor modes. zuzu uses four of them (SYS mode is also partially used):

| Mode | Value  | Used for                                |
| ---- | ------ | --------------------------------------- |
| USR  | `0x10` | User processes (PL0 — unprivileged)     |
| SVC  | `0x13` | Kernel, syscall handling, process entry |
| IRQ  | `0x12` | Hardware interrupt entry                |
| ABT  | `0x17` | Data/Prefetch abort handler entry       |
| UND  | `0x1B` | Undefined instruction handler           |



These modes all have seperate registers of their own (called **banked registers**), i.e. SP_usr and SP_irq are different and must be indiviually set before executing code in any of them. This also means that modes are seperated on a hardware level, so the kernel can switch from SVC mode to IRQ mode without manually saving CPU context (unlike processes).


ARMv7-A classifies processors modes by two rings: PL0 (unprivileged), PL1 (privileged) and PL2 (virtualization extensions). For the purposes of zuzu's development, only PL0 and PL1 are used. Every mode except USR is PL1, and is considered kernel mode. In this mode the CPU can access all kernel memory, can modify the CPSR, and manipulate the coprocessor. In USR execution mode, code cannot do any of these. ARM enforces privilege seperation using these modes, and zuzu utilizes them accordingly.

---

## TTBR0 / TTBR1 Address Space Split

ARMv7 exposes two coprocessor registers called **Translation Table Base Registers (TTBR0/1)**. They hold the base pointer to the virtual memory translation page tables. TTBR1 is used by the kernel itself to address its virtual memory, while TTBR0 is used by the currently executed userspace process. With this, zuzu avoids the overhead of copying kernel page tables, however this imposes the limitation of the ARM TTBR split. The two registers are assigned their ranges through the two bits in TTBCR.N, which does not have a 3GB user and 1GB kernel split like Linux does. This is why zuzu goes for an even split of 2GB user and 2GB kernel with N=1.

---

## Exception Vector Table

When an exception occurs in code execution, the processor jumps to the address specified by the Vector Base Address Register (VBAR) with an offset and switches to the appropriate mode, depending on the instruction. 

| Exception      | Offset |
| -------------- | ------ |
| Reset          | `0x00` |
| Undefined      | `0x04` |
| SVC            | `0x08` |
| Prefetch Abort | `0x0C` |
| Data Abort     | `0x10` |
| Reserved       | `0x14` |
| IRQ            | `0x18` |
| FIQ            | `0x1C` |


Find the full vector table on `arch/arm/exceptions/vectors.s`

zuzu's vector table is placed by the linker script at VA `C0018000` for `vexpress-a15`. It's then set with in `_start.s`:

```armasm
    /* Set VBAR to vector_table */
    ldr     r0, =vector_table
    mcr     p15, 0, r0, c12, c0, 0  @ VBAR = vector_table
    isb
```
---

## Exception Entry and the Stack Frame

When an exception occurs, the CPU automatically saves some registers on the stack before jumping to the handler. The exact registers and their order depend on the exception type and the mode it was taken from. For example, for an IRQ taken from SVC mode, the CPU saves the following on the IRQ stack. `entry.s` then saves the rest of the registers to form a complete `exception_frame_t` before calling the C handler. If the C handler returns (it may not due to a panic), the CPU restores the registers and returns from the exception, resuming execution.

```c
typedef struct exception_frame {
    uint32_t r[13];       // [0..12]  r0-r12
    uint32_t lr;          // [13]     lr_svc
    uint32_t return_pc;   // [14]     where to return (adjusted lr)
    uint32_t return_cpsr; // [15]     saved CPSR
} exception_frame_t;
```

---

## SVC Syscall ABI 

While most OS projects follow the Linux convention putting the syscall number in `r7` then executing `SVC #0`, zuzu uses `SVC #n` where `n` IS the syscall number, masked as `& 0xFF` from the 24-bit imm field. This means that the syscall number is encoded directly in the lowest byte of the immediate field of the SVC instruction itself, and can be extracted in the handler by reading the instruction at `return_pc - 4`. The 1-byte limitation arises from Thumb mode, where the SVC instruction is only 16 bits with an 8-bit immediate. To use more than 256 syscalls, we would need to switch to ARM mode and use the 32-bit SVC instruction, which has a 24-bit immediate. However, this would require compiling the entire kernel in ARM mode, which is less efficient for code density. By using the SVC immediate field for the syscall number, userspace Thumb programs can be executed.

---

## The LR Adjustment Problem

Different exception modes require different adjustments to the Link Register (LR) to get the correct return address. This is a subtle  point that is handled in the entry stubs in `entry.s`. The C code then reads `frame->return_pc` which has already been corrected. The table below summarizes the adjustments needed for each exception type:

| Exception Type | LR Value                        | Adjustment Needed       |
| -------------- | ------------------------------- | ----------------------- |
| SVC            | PC of instruction after the SVC | lr = lr (no adjustment) |
| IRQ            | PC of next instruction + 4      | lr = lr - 4             |
| Data Abort     | Faulting PC + 8                 | lr = lr - 8             |
| Prefetch Abort | Faulting PC + 4                 | lr = lr - 8             |


---

## Memory Attributes and Cache Configuration

Zuzu enforces two memory safety levels for its virtual memory mappings: Normal cacheable and Device non-cacheable. This is done by setting the appropriate bits in the page table entries. MMIO regions must be mapped as Device non-cacheable to ensure that the CPU does not cache reads and writes to these regions, because there is usually nothing to read/write from, which pollutes the cache and causes stale data manipulation. On the other hand, normal memory can be mapped as cacheable to improve performance. `ioremap()` specifically looks out for VM_MEM_DEVICE.

---

## ARM Access Permissions (AP Bits)

| AP[2:0] | PL1 (Kernel) | PL0 (User) | Use Case                     |
| ------- | ------------ | ---------- | ---------------------------- |
| 0b001   | Read/Write   | No access  | Kernel code/data             |
| 0b011   | Read/Write   | Read/Write | User code/data               |
| 0b101   | Read-only    | No access  | Kernel read-only             |
| 0b111   | Read-only    | Read-only  | User read-only (shared libs) |
| 0b000   | No access    | No access  | Unmapped / guard page        |

---

## See Also

- [memory.md](memory.md) — physical and virtual layout
- [interrupts.md](interrupts.md) — GICv2, IRQ dispatch
- [syscalls.md](syscalls.md) — full syscall ABI
- `arch/arm/exceptions/entry.s` — exception entry assembly
- `arch/arm/exceptions/exception.c` — C fault handlers
- `arch/arm/mmu/mmu.c` — page table management