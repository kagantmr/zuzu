# Zuzu Boot Process

This document traces the boot sequence from CPU reset to the kernel idle loop, explaining what happens at each stage and why.

---

## Overview

```
CPU reset
  └─ _start.s          (physical addresses, MMU off)
       └─ early.c       (physical addresses, MMU off)
            └─ kmain.c  (virtual addresses, MMU on, full kernel environment)
                 └─ idle loop (WFI, scheduler running)
```

---

## Stage 1 — Reset (`arch/arm/vexpress-a15/_start.s`)

The CPU starts executing at the physical address of the kernel image (`0x80010000` on vexpress-a15). The MMU is off. Everything runs at physical addresses.

**What `_start.s` does, in order:**

1. Disables interrupts (`CPSID if`)
2. Initializes stack pointers for all CPU modes (SVC, IRQ, ABT, UND) — each mode has its own banked SP. Stacks are placed at fixed physical addresses defined in the linker script.
3. Saves the DTB pointer from `r2` (passed by QEMU/bootloader) into a global variable before any C code runs.
4. Enables the MMU with an identity mapping and the higher-half kernel mapping simultaneously — the CPU executes through the identity range into the higher-half in a single jump.
5. After the MMU is on and the CPU is executing from `0xC0xxxxxx`, removes the identity mapping.
6. Sets up the exception vector table base address (VBAR).
7. Zeroes the BSS segment.
8. Calls `early_main()`.

<!-- TODO: add a note about the identity→higher-half transition being the trickiest part of boot. -->
<!-- The window where both mappings are live is intentionally brief — just long enough to make the jump. -->

---

## Stage 2 — Early Init (`arch/arm/vexpress-a15/early.c`)

Still in a minimal environment. The heap does not exist yet. No interrupts. `kprintf` works only if `EARLY_UART` is defined (polled UART initialized here).

**What `early.c` does:**

1. Reads the kernel layout from linker symbols (`_kernel_start`, `_kernel_end`, etc.) and populates the global `kernel_layout_t` struct.
2. Initializes the Physical Memory Manager (PMM) — the bitmap is built from the total RAM size discovered via the DTB size field in `r2`.
3. Reserves the kernel image pages in the PMM so they are never handed out as free.
4. Reserves the DTB pages.
5. Reserves all MMIO regions (hardcoded for vexpress-a15; later replaced by DTB discovery).
6. Initializes the kernel heap (`alloc_init()`).
7. Calls `kmain()`.

<!-- TODO: explain why the heap can't exist before early.c — the PMM must be up first, -->
<!-- and the PMM needs to know the memory map before it can be queried for free pages. -->

---

## Stage 3 — Kernel Main (`kernel/kmain.c`)

By the time `kmain()` runs, the full kernel environment is ready: virtual addresses are valid, the heap exists, and the PMM is operational.

**Sequence inside `kmain()`:**

1. Converts PA-based fields in `kernel_layout` to their VA equivalents.
2. Verifies the DTB magic (`0xD00DFEED`) and copies the DTB to a stable heap allocation.
3. Removes the identity mapping (`vmm_remove_identity_mapping()`).
4. Initializes TTBR1 with the kernel's address space (`arch_mmu_init_ttbr1()`), locking the kernel sections as kernel-only (AP=0b001).
5. Parses the DTB (`dtb_init()`) — discovers memory regions, MMIO bases, and device IRQ numbers.
6. Initializes the interrupt controller (`irq_init()` → GICv2 distributor + CPU interface).
7. Initializes board devices (`board_init_devices()` → SP804 timer, UART IRQ).
8. Enables interrupt-driven UART reception.
9. Globally enables IRQs (`arch_global_irq_enable()`).
10. Prints the boot banner.
11. Initializes the scheduler (`sched_init()`).
12. Creates test processes and adds them to the run queue.
13. Registers the scheduler as the tick callback.
14. Enters the idle loop (`WFI`).

<!-- TODO: expand the TTBR1 lockdown step — this is when the transition from "single shared -->
<!-- page table" to "kernel in TTBR1, per-process in TTBR0" happens. Before this step, -->
<!-- everything is in one L1 table. After it, the kernel is only reachable via TTBR1. -->

<!-- TODO: explain the boot banner function — what it prints (version string, memory stats, etc.) -->

---

## Boot Memory State Timeline

| Point in boot | MMU | Heap | PMM | IRQs |
|---------------|-----|------|-----|------|
| `_start.s` begins | Off | No | No | Disabled |
| MMU enable (identity+higher-half) | On | No | No | Disabled |
| `early.c` begins | On | No | No | Disabled |
| After `pmm_init()` | On | No | Yes | Disabled |
| After `alloc_init()` | On | Yes | Yes | Disabled |
| `kmain()` begins | On | Yes | Yes | Disabled |
| After `arch_global_irq_enable()` | On | Yes | Yes | **Enabled** |
| After `sched_init()` + `register_tick_callback()` | On | Yes | Yes | Enabled + ticking |

---

## See Also

- `arch/arm/vexpress-a15/_start.s` — reset entry, mode stacks, MMU enable
- `arch/arm/vexpress-a15/early.c` — PMM, heap init, DTB preservation
- `kernel/kmain.c` — full kernel initialization sequence
- [memory.md](memory.md) — layout of physical and virtual address space
- [arch.md](arch.md) — TTBR split, exception model