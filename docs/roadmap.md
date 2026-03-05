zuzu Kernel Roadmap

## Phase 0: Project Genesis & Build System 
- [x] Repository structure (arch/, kernel/, drivers/, lib/)
- [x] Cross-compile Makefile
- [x] Linker script defining kernel layout
- [x] Versioned README and LICENSE
- [x] Assertion and panic infrastructure

---

## Phase 1: Boot & Early Execution
- [x] _start.s entry point
- [x] CPU in SVC mode
- [x] Stack initialized
- [x] BSS zeroed
- [x] Early stack switching (larger kernel stack)
- [x] Boot path isolation (early.c vs kmain.c)
- [x] Minimal exception stubs (initial)

---

## Phase 2: Early Output & Diagnostics
- [x] PL011 UART driver (polled)
- [x] Early UART output usable before MMU
- [x] Formatted printing (kprintf)
- [x] Logging macros (info/warn/panic)
- [x] Panic halt semantics

---

## Phase 3: Physical Memory Management
- [x] Page size definition (4 KiB)
- [x] Bitmap-based PMM
- [x] Early reservation of kernel image
- [x] Early reservation of DTB
- [x] Early reservation of MMIO regions
- [x] PFN to physical address helpers 

---

## Phase 4: Hardware Discovery (DTB)
- [x] Flattened Device Tree (DTB) parser
- [x] In-place DTB walking
- [x] /memory node parsing
- [x] Reserved memory handling
- [x] MMIO base discovery

---

Phase 5: Kernel Heap
- [x] Heap initialization
- [x] kmalloc
- [x] kfree
- [x] Debug and sanity checks

---

## Phase 6: Virtual Memory & MMU
- [x] ARMv7 MMU support
- [x] L1 page tables (section mapping)
- [x] Identity mapping for boot
- [x] Higher-half kernel mapping (0xC0000000)
- [x] MMU enable sequence
- [x] Identity mapping removal
- [x] Stable kernel virtual layout
- [x] Mapping helpers (map, unmap)

---

## Phase 6.5: VMM Extensions
- [x] ioremap(phys, size) for device mappings
- [x] iounmap(va) cleanup
- [x] Reserved VA range for MMIO (0xF0000000+)

---

## Phase 7: Exceptions & CPU State Management 
- [x] Exception vector table (vectors.s)
- [x] Entry stubs (entry.s)
- [x] Full register save/restore
- [x] Mode-specific stacks (SVC, IRQ, ABT, UND)
- [x] Panic on unhandled exception
- [x] Detailed fault decoding (DFSR/IFSR)

---

## Phase 8: Interrupt Subsystem
- [x] Interrupt-driven UART I/O
- [x] SP804 timer driver
- [x] SP804 periodic tick
- [x] ARM generic timer tick
- [x] IRQ dispatch mechanism
- [x] IRQ enable/disable primitives
- [x] IRQ registration API
- [x] GICv2 CPU interface initialization
- [x] GICv2 distributor initialization

---

## Phase 8.5: MMIO Cleanup
- [x] ioremap(phys, size) for device mappings
- [x] iounmap(va) cleanup
- [x] Reserved VA range for MMIO (0xF0000000+)

---

## Phase 9: Time & Scheduling Foundations
- [x] Periodic timer source selection
- [x] Global tick counter
- [x] Preemption point definition
- [x] Scheduler entry from IRQ

---

## Phase 10: Process Model & Address Space Isolation
- [x] Process Control Block (PCB) structure
- [x] Process states (READY, RUNNING, BLOCKED, ZOMBIE)
- [x] Per-process kernel stack
- [x] Kernel stack guard pages
- [x] L2 page tables (4KB pages)
- [x] L2 table pool for fast allocation
- [x] Per-process address spaces (TTBR0 per process)
- [x] TTBR1 for kernel mappings (set once at boot)
- [x] Context switch routine (switch.s)
- [x] Round-robin scheduler
- [x] Backtrace/stack unwinding
- [ ] Kernel symbol lookup (addr to function name)

---

## Phase 11: Privilege Separation
- [x] SVC syscall entry (entry.s)
- [x] Syscall number extraction from SVC immediate (& 0xFF)
- [x] Syscall dispatch table
- [x] Syscall ABI definition (docs/syscalls.md)
- [x] User stack setup
- [x] USR mode entry via exception return frame
- [x] AP bit enforcement (kernel-only sections locked down)

---

## Phase 12: Syscall ABI (SVC Immediate Dispatch)

- [x] SVC syscall entry (entry.s)
- [x] Syscall number extraction from SVC immediate (& 0xFF)
- [x] Syscall dispatch table
- [x] Syscall ABI definition (docs/syscalls.md)
- [x] User pointer validation (validate_user_ptr)
- [x] task_exit syscall
- [x] task_yield syscall
- [x] task_sleep syscall
- [x] get_pid syscall
- [x] task_spawn syscall (exposed as syscall ABI wrapper) *(see Phase 20 + Phase 17)*
- [x] task_wait syscall (exposed as syscall ABI wrapper) *(see Phase 20 + Phase 17)*

---

## Phase 13: IPC (Synchronous Message Passing)

- [x] Endpoint kernel object (sender/receiver queues)
- [x] Handle table per process (16 slots)
- [x] proc_send (SVC #0x10)
- [x] proc_recv (SVC #0x11)
- [x] proc_call (SVC #0x12)
- [x] proc_reply (SVC #0x13)
- [x] port_create (SVC #0x20)
- [x] port_destroy (SVC #0x21)
- [x] port_grant (SVC #0x22)
- [x] Blocking send/recv with scheduler integration
- [x] ERR_DEAD on endpoint destroyed while waiting
- [ ] Capability transfer through IPC messages *(required for Phase 18)*

---

## Phase 14: IRQ Forwarding (Interrupts to Userspace)

- [x] sys_irq_claim: process claims an IRQ line
- [x] sys_irq_wait: block until claimed IRQ fires
- [x] sys_irq_done: unmask IRQ line after handling
- [x] Kernel IRQ path routes to waiting driver process
- [ ] Which IRQs are kernel-only vs forwardable (timer stays kernel)

---

## Phase 15: Userspace Drivers (MMIO Mapping + First Driver)

- [x] sys_mapdev: map MMIO into driver process address space
- [x] Userspace PL011 UART driver
- [ ] Console IPC protocol (other tasks print by IPC to UART server)
- [x] Kernel stops using UART (mostly) after handoff (except early boot)

---

## Phase 16: Program Loading (Initrd + ELF)

- [x] Initrd/ramdisk in kernel image (CPIO or raw)
- [x] ELF header parsing (PT_LOAD segments)
- [x] BSS zeroing for ELF segments
- [x] process_create_from_elf()
- [x] User process bootstrap (crt0, _start)
- [x] Init process (PID 1)

---

## Phase 17: Userspace C Runtime (crt0 + Syscall Wrappers)

- [x] crt0.s (_start → main → exit)
- [x] User-side linker script
- [x] Separate build targets for kernel and userspace
- [x] "Hello World" user process in C
- [ ] Syscall wrapper library (SVC #n wrappers)

---

## Phase 18: Service Registry (Name Server + Name Resolution)

- [x] Name server process
- [x] Handle 0 pre-populated in every process
- [x] register / lookup IPC protocol
- [x] Capability transfer (port_grant through IPC) *(depends on Phase 13 capability transfer)*

---

## Phase 19: Advanced Memory Management

- [x] sys_mmap / sys_munmap
- [x] Demand paging (Data Abort on unmapped user page → allocate)
- [ ] Shared memory objects (sys_mshare / sys_attach)

---

## Phase 20: Process Lifecycle (Spawn/Wait + Cleanup)

- [x] task_spawn: load ELF from initrd, return child PID
- [x] task_wait: block until child exits, return exit status
- [ ] Orphan re-parenting to init
- [ ] Zombie reaping

---

## Phase 21: Filesystem Server (Userspace)

- [ ] In-memory FS server (initrd-backed)
- [ ] OPEN / READ / CLOSE IPC protocol
- [ ] Per-client file descriptor state
- [ ] Register as "fs" with name server

---

## Phase 22: NIC Driver Server (Userspace)

- [ ] LAN9118 MMIO discovery from DTB
- [ ] Userspace NIC driver process
- [ ] LAN9118 initialization sequence
- [ ] Ethernet frame TX/RX
- [ ] Register as "nic0" with name server

---

## Phase 23: Network Stack Server (L2/L3)

- [ ] Ethernet frame parsing/dispatch
- [ ] ARP table + ARP request/reply handling
- [ ] IPv4 send/receive path
- [ ] ICMP echo (ping)
- [ ] Register as "net" with name server

---

## Phase 24: Transport Layer (UDP/TCP)

- [ ] UDP (send/recv, port binding)
- [ ] TCP (handshake, basic data transfer)
- [ ] DNS client (over UDP)

---

## Phase 25: Network Applications

- [ ] Socket-like user library (IPC to transport server)
- [ ] DHCP client
- [ ] TCP echo server
- [ ] HTTP client (stretch)

---

## Phase 26: Hardening and Polish

- [ ] ASID support (reduce/eliminate full TLB flushes)
- [ ] Kernel symbol table embedded in image (panic backtraces with names)
- [ ] copy_from_user / copy_to_user with page-table walk validation
- [ ] Watchdog: flag processes that don't yield within N ticks
- [ ] LDREX/STREX atomic support for userspace
- [ ] Cache maintenance on ELF code segment load (D-cache clean, I-cache invalidate)

