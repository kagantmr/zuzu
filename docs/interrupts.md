# Zuzu Interrupt Subsystem

This document covers how hardware interrupts are handled in Zuzu: the GICv2 controller, the IRQ registration API, timer configuration, and the relationship between interrupts and the scheduler.

---

## GICv2 Overview

Zuzu uses the ARM Generic Interrupt Controller version 2, which is the interrupt controller on the vexpress-a15 platform. GICv2 has two distinct components:

**GICD — Distributor** (`0x2C001000`)  
Receives interrupt signals from all connected peripherals. Routes each interrupt to one or more CPU interfaces. Responsible for enable/disable per interrupt line, priority configuration, and target CPU selection.

**GICC — CPU Interface** (`0x2C002000`)  
Per-CPU register interface. The running CPU reads the CPU interface to acknowledge an interrupt (getting its ID from the IAR register), and writes to the EOIR register to signal end-of-interrupt. The CPU interface also controls the priority mask — the minimum priority level the CPU will accept.

<!-- TODO: explain how the two components interact: -->
<!-- GICD asserts a virtual IRQ line to the GICC. The CPU receives the signal as an IRQ exception. -->
<!-- The CPU reads GICC_IAR to acknowledge — this returns the interrupt ID and marks it as active. -->
<!-- The CPU handles the interrupt, then writes the same ID to GICC_EOIR. -->
<!-- Only then does GICD consider the interrupt fully handled and allow re-triggering. -->

---

## IRQ Numbers on vexpress-a15

GICv2 uses a flat namespace. Interrupt IDs 0–15 are Software Generated Interrupts (SGIs). IDs 16–31 are Private Peripheral Interrupts (PPIs) — per-CPU, like the generic timer. IDs 32+ are Shared Peripheral Interrupts (SPIs) — the device interrupts.

| IRQ ID | Device | Notes |
|--------|--------|-------|
| 27 | ARM Generic Timer (PPI) | Per-CPU, used for preemption tick |
| 34 | SP804 Timer 0 | Dual-timer module, channel 0 |
| 35 | SP804 Timer 1 | Dual-timer module, channel 1 |
| 36 | RTC (PL031) | Real-time clock |
| 37 | UART0 (PL011) | Primary console UART |
| 38 | UART1 | |
| 39 | UART2 | |
| 40 | UART3 | |

<!-- TODO: verify these IRQ numbers against your DTB parser output / board.h -->

---

## IRQ Registration API (`arch/arm/irq/irq.c`)

<!-- TODO: document the irq_register_handler() function signature and semantics -->
<!-- - takes an IRQ number and a function pointer -->
<!-- - stores in a dispatch table indexed by IRQ ID -->
<!-- - called during device init (pl011_init_irq, sp804_init, etc.) -->

<!-- TODO: document irq_enable(n) / irq_disable(n) — per-line enable/disable via GICD_ISENABLERn -->

<!-- TODO: document arch_global_irq_enable() / arch_global_irq_disable() — CPSR I-bit -->

---

## IRQ Dispatch Flow

When an IRQ fires:

```
Hardware asserts IRQ line
  → GIC asserts CPU IRQ signal
    → CPU takes IRQ exception (entry.s irq_handler stub)
      → saves registers (SRSDB + STMFD)
        → reads GICC_IAR to acknowledge and get IRQ ID
          → calls registered handler for that IRQ ID
            → writes GICC_EOIR (end of interrupt)
              → RFEIA restores registers and returns to interrupted code
```

<!-- TODO: trace through the actual assembly in entry.s for the IRQ path -->
<!-- Highlight the sub lr, lr, #4 that is required for IRQ but NOT for SVC -->

---

## Timer Configuration

Zuzu supports two timer sources:

### SP804 Dual Timer (`drivers/timer/sp804.c`)

<!-- TODO: explain: -->
<!-- - SP804 is a legacy timer on the vexpress-a15 motherboard -->
<!-- - Configured as a free-running periodic interrupt source -->
<!-- - Used during early bring-up before the generic timer was available -->
<!-- - Still present and initialized, but the generic timer is the primary tick source -->

### ARM Generic Timer (`arch/arm/timer/generic_timer.c`)

<!-- TODO: explain: -->
<!-- - Part of the Cortex-A15 core itself, not a memory-mapped peripheral -->
<!-- - Programmed via system registers (CNTP_TVAL, CNTP_CTL) -->
<!-- - PPI interrupt ID 27 — private to each CPU, always available -->
<!-- - Higher precision than SP804, more portable across ARM platforms -->
<!-- - Used as the primary preemption tick source in the current build -->

---

## Tick and Scheduler Integration

<!-- TODO: explain the tick callback mechanism: -->
<!-- - register_tick_callback(fn) installs a function called from the timer IRQ handler -->
<!-- - schedule() is registered as the tick callback in kmain() -->
<!-- - This means: on every timer tick, the scheduler runs and may switch processes -->
<!-- - The scheduler can also be triggered by blocking syscalls (IPC send/recv) -->

<!-- TODO: explain tick counter (kernel/time/tick.c) — global monotonic counter incremented every tick -->
<!-- Used by task_sleep to wake processes at a future tick. -->

---

## Interrupt Priority Configuration

<!-- TODO: document what priority levels you've assigned to which interrupts -->
<!-- GICv2 supports up to 256 priority levels (0 = highest). vexpress-a15 typically implements fewer. -->
<!-- Currently all interrupts probably use the same priority — note that and say it's fine for now. -->

---

## See Also

- `arch/arm/irq/gicv2.c` — GICv2 distributor and CPU interface initialization
- `arch/arm/irq/irq.c` — IRQ registration dispatch table
- `arch/arm/exceptions/entry.s` — IRQ entry/exit assembly
- `drivers/timer/sp804.c` — SP804 driver
- `arch/arm/timer/generic_timer.c` — Generic timer driver
- `kernel/time/tick.c` — global tick counter
- [arch.md](arch.md) — exception model, entry/exit mechanics