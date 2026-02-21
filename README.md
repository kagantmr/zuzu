# zuzu

![Statistics screen](img/boot_stats.png)

Zuzu is a microkernel targeting AArch32 / ARMv7-A. It's written in C and ARM assembly from scratch. The kernel runs on QEMU's `vexpress-a15` machine (Cortex-A15) and is designed from the very first principles around microkernel principles: a minimal kernel, strict process isolation, and I/O through inter-process messaging (IPC).

This project started as a hobby project and grew into a full systems programming exploration. The goal is a complete, understandable microkernel, something that demonstrates how a real OS kernel works at every level.

<!-- TODO: one or two sentences about what drives you to work on this — reviewers respond to motivation -->

---

## Status

Phases 0–12 is completed. Synchronous IPC is implemented and tested.

| Phase | Name | Status |
|-------|------|--------|
| 0–2 | Build system, boot, UART, diagnostics | done |
| 3–4 | Physical memory manager, DTB parser | done |
| 5 | Kernel heap (kmalloc/kfree) | done |
| 6 | Exception vectors, fault decoding | done |
| 7 | MMU, higher-half kernel, ioremap | done |
| 8 | GICv2, interrupt subsystem, SP804/generic timer | done |
| 9 | Preemptive scheduler, tick | done |
| 10 | Per-process address spaces, L2 tables, context switch | done |
| 11 | User mode (USR), privilege separation, syscall ABI | done |
| 12 | Synchronous IPC — send, recv, call, reply | done |
| 13 | ELF loader, initrd, program loading | next |
| 14 | Userspace C runtime, crt0, syscall wrappers | planned |
| 15–17 | Networking (NIC, UDP, TCP) | planned |

---

## Design Choices

<!-- TODO: expand each of these into 1–2 sentences of explanation. The "why" matters more than the "what" to a reviewer. -->

**SVC immediate used as syscall number.** Zuzu encodes the syscall number directly in the lower byte of the `SVC #n` instruction's immediate field, rather than the Linux convention of placing it in `r7`. This was chosen to reduce excess register usage and make use of the 24 bits. Now, due to the incompatibility between ARM mode and Thumb mode (Thumb `SVC #n` immediate is 8 bits), the full 24-bits of the ARM mode `SVC #n` instruction are not used.

**TTBR0 / TTBR1 address space split.** The MMU uses two translation table base registers simultaneously: TTBR1 holds the kernel's page table and never changes, TTBR0 holds the current process's page table and is swapped on every context switch. The split boundary is at `0x80000000` (TTBCR.N = 2). This means kernel mappings are always accessible without any per-process copying.

**Higher-half kernel.** The kernel runs at virtual address `0xC0000000` (physical `0x80000000`). User processes occupy `0x00000000–0x7FFFFFFF` due to the TTBR split.

**Synchronous rendezvous IPC.** Message passing uses a blocking rendezvous model: `send` blocks until a receiver is ready, `recv` blocks until a sender arrives. When both are present, the kernel copies four registers (r0-r3) and unblocks both. No queues or heap allocation for now.

**GICv2 interrupt controller.** Interrupt routing is handled through the ARM Generic Interrupt Controller v2 (GICv2) with a distributor and per-CPU interface. The kernel handles a small set of interrupts in-kernel (timer, UART) with the infrastructure in place for forwarding to userspace drivers.

![Panic screen](img/panic.png)

---

## Supported Targets

| Board | Machine | CPU | Status |
|-------|---------|-----|--------|
| QEMU vexpress-a15 | `-M vexpress-a15` | Cortex-A15 | primary |
| QEMU vexpress-a9 | `-M vexpress-a9` | Cortex-A9 | planned |
| Raspberry Pi 4 | — | Cortex-A72 (32-bit mode) | planned |
| STM32 (MPU) | — | Cortex-M | planned |

---

## Prerequisites

### Linux (Debian/Ubuntu)
```bash
sudo apt update
sudo apt install gcc-arm-none-eabi qemu-system-arm make
```

### macOS (Homebrew)
```bash
brew install --cask gcc-arm-embedded
brew install qemu
```

---

## Building & Running

**Build:**
```bash
make
```

**Run on QEMU:**
```bash
make run
```

**Debug (GDB):**

In one terminal, start QEMU in halted state waiting for a debugger:
```bash
make debug
```


In another, attach GDB:
```bash
arm-none-eabi-gdb build/zuzu.elf
(gdb) set architecture armv7
(gdb) target remote :1234
(gdb) continue
```

To find more debugging info, see `docs/debugging.md`.

---

## Source Structure

```
zuzu/
├── arch/
│   └── arm/
│       ├── vexpress-a15/      # Board entry point, linker script, early init
│       ├── mmu/               # ARMv7 page table management, L2 pool
│       ├── exceptions/        # Exception vectors, entry stubs, fault decoder
│       ├── irq/               # GICv2 driver, IRQ dispatch
│       ├── timer/             # ARM CP15 generic timer
│       └── include/           # ARM-specific headers (context, GICv2, IRQ)
├── kernel/
│   ├── dtb/                   # Flattened Device Tree parser
│   ├── mm/                    # PMM (bitmap), kernel heap (kmalloc/kfree)
│   ├── vmm/                   # Virtual memory manager, addrspace, ioremap
│   ├── proc/                  # Process control blocks, kernel stacks
│   ├── sched/                 # Round-robin scheduler, sys_task syscalls
│   ├── ipc/                   # Endpoints, ports, send/recv/call/reply
│   ├── syscall/               # Syscall dispatch table, ABI, pointer validation
│   ├── time/                  # Global tick counter
│   └── stats/                 # Runtime statistics
├── drivers/
│   ├── uart/                  # PL011 UART
│   └── timer/                 # SP804 dual timer
├── core/                      # kprintf, panic, logging macros, version, assert
└── lib/                       # memcpy/memset, string, snprintf, list, convert
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/boot.md](docs/boot.md) | Boot sequence from cold reset to the idle loop of `kmain()` |
| [docs/arch.md](docs/arch.md) | ARM architecture decisions — TTBR split, SVC ABI, exception model |
| [docs/debugging.md](docs/debugging.md) | Debugging help |
| [docs/memory.md](docs/memory.md) | Physical and virtual memory layout, page table format |
| [docs/interrupts.md](docs/interrupts.md) | GICv2, IRQ registration, timer configuration |
| [docs/ipc.md](docs/ipc.md) | IPC model: endpoints, ports, rendezvous, handle tables |
| [docs/processes.md](docs/processes.md) | Process model, PCB layout, context switch, scheduler |
| [docs/syscalls.md](docs/syscalls.md) | Full syscall ABI reference |
| [docs/roadmap.md](docs/roadmap.md) | Phase-by-phase development roadmap |

---

## License

MIT License. See [LICENSE](LICENSE) for details.