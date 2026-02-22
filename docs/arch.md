# zuzu Architecture Document

This document covers the key ARM-specific design decisions in zuzu: why they were made, what the alternatives were, and what constraints they impose going forward.

---

## ARM Processor Modes

ARMv7-A has seven processor modes. zuzu uses four of them (SYS mode is also partially used):

| Mode | Value | Used for |
|------|-------|----------|
| USR  | `0x10` | User processes (PL0 — unprivileged) |
| SVC  | `0x13` | Kernel, syscall handling, process entry |
| IRQ  | `0x12` | Hardware interrupt entry |
| ABT  | `0x17` | Data/Prefetch abort handler entry |
| UND  | `0x1B` | Undefined instruction handler |



These modes all have seperate registers of their own (called banked registers), i.e. SP_usr and SP_irq are different and must be indiviually set before executing code in any of them. This also means that modes are seperated on a hardware level, so the kernel can switch from SVC mode to IRQ mode without manually saving CPU context (unlike processes).


ARMv7-A classifies processors modes by two rings: PL0 (unprivileged), PL1 (privileged) and PL2 (virtualization extensions). For the purposes of zuzu's development, only PL0 and PL1 are used. Every mode except USR is PL1, and is considered kernel mode. In this mode the CPU can access all kernel memory, can modify the CPSR, and manipulate the coprocessor. In USR execution mode, code cannot do any of these. ARM enforces privilege seperation using these modes, and zuzu utilizes them accordingly.

---

## TTBR0 / TTBR1 Address Space Split

ARMv7 exposes two coprocessor registers called Translation Table Base Registers (TTBR0/1). They hold the base pointer to the virtual memory translation page tables. TTBR1 is used by the kernel itself to address its virtual memory, while TTBR0 is used by the currently executed userspace process. With this, zuzu avoids the overhead of copying kernel page tables, however this imposes the limitation of the ARM TTBR split. The two registers are assigned their ranges through the two bits in TTBCR.N, which does not have a 3GB user and 1GB kernel split like Linux does. This is why zuzu goes for an even split of 2GB user and 2GB kernel with N=1.
---

## Exception Vector Table

When an exception occurs in code execution, the processor jumps to the address specified by the Vector Base Address Register (VBAR) with an offset and switches to the appropriate mode, depending on the instruction. 

| Exception | Offset |
|------|-------|
| Reset  | `0x00` | 
| Undefined  | `0x04` | 
| SVC  | `0x08` | 
| Prefetch Abort  | `0x0C` |
| Data Abort  | `0x10` | 
| Reserved | `0x14` |
| IRQ | `0x18` |
| FIQ | `0x1C` |
[arch/arm/exceptions/vectors.s](arch/arm/exceptions/vectors.s)

zuzu's vector table is placed by the linker script at VA `C0018000` for `vexpress-a15`. It's then set with in `_start.s`:

```armasm
    /* Set VBAR to vector_table */
    ldr     r0, =vector_table
    mcr     p15, 0, r0, c12, c0, 0  @ VBAR = vector_table
    isb
```
---

## Exception Entry and the Stack Frame

<!-- TODO: walk through what entry.s actually does: -->
<!-- 1. Mode-specific LR adjustment (IRQ needs sub lr, lr, #4; SVC does NOT; Data Abort is different) -->
<!-- 2. SRSDB: pushes LR and SPSR onto the target mode stack -->
<!-- 3. CPSID: disables interrupts for the duration of kernel handling -->
<!-- 4. Saving r0–r12 and user-mode SP/LR with ^ suffix -->
<!-- 5. Calling the C handler with a pointer to the exception_frame_t on the stack -->
<!-- 6. RFEIA: atomically restores PC and CPSR from the stack frame (mode switch happens here) -->

<!-- TODO: show the exception_frame_t layout from context.h — which registers are saved in which order.
     This is important because the IPC code directly manipulates frame->r[0]–r[3] to pass return values. -->

---

## SVC Syscall ABI — Why the Immediate Field

<!-- TODO: explain the design decision: -->
<!-- Most ARM OS projects (including Linux) put the syscall number in r7 and execute SVC #0. -->
<!-- Linux did this for Thumb compatibility — Thumb SVC has only an 8-bit immediate, same as ARM, -->
<!-- but the *same* 8-bit space is what zuzu uses for the syscall number. -->
<!-- zuzu uses SVC #n where n IS the syscall number, masked as & 0xFF from the 24-bit imm field. -->
<!-- -->
<!-- How extraction works: -->
<!--   return_pc = frame->return_pc           // points to instruction AFTER the SVC -->
<!--   svc_instr = *(uint32_t *)(return_pc - 4)  // the SVC instruction itself -->
<!--   svc_num   = svc_instr & 0xFF           // low 8 bits = syscall number -->
<!-- -->
<!-- Why 8 bits and not 24: the full 24-bit space is available but using 8 gives 256 syscalls — -->
<!-- more than enough and keeps the dispatch table a fixed array rather than a hash. -->
<!-- -->
<!-- Trade-off: commits zuzu to ARM-mode-only userspace. Thumb userspace would need a different -->
<!-- extraction path (the SVC in Thumb is 16-bit with only an 8-bit imm). Not a problem for now. -->

---

## The LR Adjustment Problem

<!-- TODO: This is a subtle but important correctness point. Explain: -->
<!-- Different exceptions leave LR_mode pointing to different things: -->
<!-- - SVC:           LR_svc = PC of instruction AFTER the SVC (correct for return) -->
<!-- - IRQ:           LR_irq = PC of next instruction + 4 (must subtract 4) -->
<!-- - Data Abort:    LR_abt = faulting PC + 8 (must subtract 8 to get faulting addr) -->
<!-- - Prefetch Abort: LR_abt = faulting PC + 4 (must subtract 4) -->
<!-- -->
<!-- The entry stubs in entry.s handle this per-mode. The C code then reads frame->return_pc -->
<!-- which has already been corrected. For SVC specifically: the LR is NOT adjusted before -->
<!-- saving, and the syscall number is read from (return_pc - 4) in C. -->

---

## Memory Attributes and Cache Configuration

<!-- TODO: explain the two memory types zuzu uses (from vmm.h): -->
<!-- VM_MEM_NORMAL: TEX=001, C=1, B=1 — Write-Back Write-Allocate, cacheable -->
<!-- VM_MEM_DEVICE: TEX=000, C=0, B=1 — Shared Device, non-cacheable, strongly ordered -->
<!-- -->
<!-- Why it matters: mapping MMIO as Normal cacheable causes silent data corruption. -->
<!-- The CPU may read from cache instead of the device register. -->
<!-- ioremap() always uses VM_MEM_DEVICE. -->

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