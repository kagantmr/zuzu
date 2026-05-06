# zuzu Interrupt Subsystem

This document covers how hardware interrupts are handled in zuzu: the GICv2 controller, the IRQ registration API, timer configuration, and the relationship between interrupts and the scheduler.

---

## GICv2 Overview

zuzu uses the ARM Generic Interrupt Controller version 2, which is the interrupt controller on the vexpress-a15 platform. GICv2 has two distinct components:

**GICD — Distributor** (`0x2C001000` on vexpress-a15)  
Receives interrupt signals from all connected peripherals. Routes each interrupt to one or more CPU interfaces. Responsible for enable/disable per interrupt line, priority configuration, and target CPU selection.

**GICC — CPU Interface** (`0x2C002000` on vexpress-a15)  
Per-CPU register interface. The running CPU reads the CPU interface to acknowledge an interrupt (getting its ID from the IAR register), and writes to the EOIR register to signal end-of-interrupt. The CPU interface also controls the priority mask — the minimum priority level the CPU will accept.

---

## IRQ Numbers on vexpress-a15

GICv2 uses a flat namespace. Interrupt IDs 0–15 are Software Generated Interrupts (SGIs). IDs 16–31 are Private Peripheral Interrupts (PPIs) — per-CPU, like the generic timer. IDs 32+ are Shared Peripheral Interrupts (SPIs) — the device interrupts.

| IRQ ID | Device                  | Notes                             |
| ------ | ----------------------- | --------------------------------- |
| 27     | ARM Generic Timer (PPI) | Per-CPU, used for preemption tick |
| 34     | SP804 Timer 0           | Dual-timer module, channel 0      |
| 35     | SP804 Timer 1           | Dual-timer module, channel 1      |
| 36     | RTC (PL031)             | Real-time clock                   |
| 37     | UART0 (PL011)           | Primary console UART              |
| 38     | UART1                   |                                   |
| 39     | UART2                   |                                   |
| 40     | UART3                   |                                   |

---

## IRQ Registration API (`arch/arm/irq/irq.c`)

To handle an interrupt, a device driver registers a handler function for the corresponding IRQ ID. When an interrupt fires, the GICv2 CPU interface provides the IRQ ID to the kernel, which looks up the registered handler and calls it. For example, the SP804 timer driver registers handlers for IRQs 34 and 35, and the ARM generic timer driver registers a handler for IRQ 27. The PL011 UART driver registers a handler for IRQ 37 to handle incoming data and transmit completion.

An IRQ line can be enabled or disabled via the GIC distributor's ISENABLER/ICENABLER registers. By default, all IRQ lines are disabled at boot. Drivers must explicitly enable their IRQ lines after registering handlers.

To disable all interrupts globally (e.g., during critical sections), the kernel sets the I-bit in the CPSR register. This is a coarse-grained mechanism and should be used sparingly to avoid missing important interrupts.
---

## IRQ Dispatch Flow

When an IRQ fires:


1. Hardware asserts IRQ line
2. GIC asserts CPU IRQ signal
3. CPU takes IRQ exception (entry.s irq_handler stub)
4. saves registers (SRSDB + STMFD)
5. reads GICC_IAR to acknowledge and get IRQ ID
6. calls registered handler for that IRQ ID
7. writes GICC_EOIR (end of interrupt)
8. RFEIA restores registers and returns to interrupted code
---

## Timer Configuration

zuzu supports two timer sources:

### SP804 Dual Timer (`drivers/timer/sp804.c`)

The SP804 is a legacy dual-timer on the vexpress-a15 motherboard that can be used as a periodic interrupt source. zuzu keeps this optional because the timer is old, and is largely unneeded due to the existence of the coprocessor generic timer. You can configure its usage via the Makefile.


### ARM Generic Timer (`arch/arm/timer/generic_timer.c`)

This timer sits within the ARM coprocessor (CP15) and is available on all ARMv7 platforms. It has two modes: a per-CPU physical timer (PPI 27 on vexpress-a15) and a global virtual timer (SPI, not used in this build). The physical timer is the primary preemption tick source in the current build. It is more precise than the SP804, and is programmed via system registers (`mrc` and `mcr` privileged instructions) rather than MMIO. It's more precise and provides more uptime than the SP804, and is more portable across ARM platforms. zuzu currently uses it as the primary tick source.

---

## Tick and Scheduler Integration

`register_tick_callback()` allows the scheduler to run on every timer tick, enabling preemptive multitasking. When a tick occurs, the scheduler can decide to switch to a different process if the current one has exhausted its time slice or if a higher-priority process is ready.

`kernel/time/tick.c` implements a global tick counter that increments on every timer tick. This allows processes to sleep until a specific future tick, enabling timed waits without busy-waiting.
---

## Interrupt Priority Configuration

GICv2 allows each interrupt to be assigned a priority level. The CPU interface has a priority mask register that determines the minimum priority level the CPU will accept. This allows critical interrupts (e.g., timer) to preempt less critical ones (e.g., UART). In the current build, all interrupts are assigned the same priority, which is sufficient for basic functionality. Future builds may differentiate priorities as needed.

---

## See Also

- `arch/arm/irq/gicv2.c` — GICv2 distributor and CPU interface initialization
- `arch/arm/irq/irq.c` — IRQ registration dispatch table
- `arch/arm/exceptions/entry.s` — IRQ entry/exit assembly
- `drivers/timer/sp804.c` — SP804 driver
- `arch/arm/timer/generic_timer.c` — Generic timer driver
- `kernel/time/tick.c` — global tick counter
- [arch.md](arch.md) — exception model, entry/exit mechanics