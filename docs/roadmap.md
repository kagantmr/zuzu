Zuzu Kernel Roadmap

Phase 0: Project Genesis & Build System 
- [x] Repository structure (arch/, kernel/, drivers/, lib/)
- [x] Cross-compile Makefile
- [x] Linker script defining kernel layout
- [x] Versioned README and LICENSE
- [x] Assertion and panic infrastructure

Phase 1: Boot & Early Execution
- [x] _start.s entry point
- [x] CPU in SVC mode
- [x] Stack initialized
- [x] BSS zeroed
- [x] Early stack switching (larger kernel stack)
- [x] Boot path isolation (early.c vs kmain.c)
- [x] Minimal exception stubs (initial)

Phase 2: Early Output & Diagnostics
- [x] PL011 UART driver (polled)
- [x] Early UART output usable before MMU
- [x] Formatted printing (kprintf)
- [x] Logging macros (info/warn/panic)
- [x] Panic halt semantics

Phase 3: Physical Memory Management
- [x] Page size definition (4 KiB)
- [x] Bitmap-based PMM
- [x] Early reservation of kernel image
- [x] Early reservation of DTB
- [x] Early reservation of MMIO regions
- [x] PFN to physical address helpers 

Phase 4: Hardware Discovery (DTB)
- [x] Flattened Device Tree (DTB) parser
- [x] In-place DTB walking
- [x] /memory node parsing
- [x] Reserved memory handling
- [x] MMIO base discovery

Phase 5: Kernel Heap
- [x] Heap initialization
- [x] kmalloc
- [x] kfree
- [x] Debug and sanity checks

Phase 6: Virtual Memory & MMU
- [x] ARMv7 MMU support
- [x] L1 page tables (section mapping)
- [x] Identity mapping for boot
- [x] Higher-half kernel mapping (0xC0000000)
- [x] MMU enable sequence
- [x] Identity mapping removal
- [x] Stable kernel virtual layout
- [x] Mapping helpers (map, unmap)

Phase 6.5: VMM Extensions
- [x] ioremap(phys, size) for device mappings
- [x] iounmap(va) cleanup
- [x] Reserved VA range for MMIO (0xF0000000+)

Phase 7: Exceptions & CPU State Management 
- [x] Exception vector table (vectors.s)
- [x] Entry stubs (entry.s)
- [x] Full register save/restore
- [x] Mode-specific stacks (SVC, IRQ, ABT, UND)
- [x] Panic on unhandled exception
- [ ] Detailed fault decoding (DFSR/IFSR)

Phase 8: Interrupt Subsystem
- [x] Interrupt-driven UART I/O
- [x] SP804 timer driver
- [x] SP804 periodic tick
- [x] ARM generic timer tick
- [x] IRQ dispatch mechanism
- [x] IRQ enable/disable primitives
- [x] IRQ registration API
- [x] GICv2 CPU interface initialization
- [x] GICv2 distributor initialization

Phase 8.5: MMIO Cleanup
- [x] ioremap(phys, size) for device mappings
- [x] iounmap(va) cleanup
- [x] Reserved VA range for MMIO (0xF0000000+)

Phase 9: Time & Scheduling Foundations
- [x] Periodic timer source selection
- [x] Global tick counter
- [x] Preemption point definition
- [x] Scheduler entry from IRQ

Phase 10: Process Model
- [x] Process Control Block (PCB) structure
- [x] Process states (READY, RUNNING, BLOCKED, ZOMBIE)
- [x] Per-process kernel stack
- [x] L2 page tables (4KB pages)
- [x] Per-process address spaces
- [x] Context switch routine
- [x] Round-robin scheduler
- [ ] Kernel symbol lookup (addr to function name)
- [x] Backtrace/stack unwinding

Phase 11: Privilege Separation & Syscalls
- [x] SVC syscall entry
- [x] Syscall ABI definition
- [x] User stack setup
- [x] SVC to USR transition
- [ ] write syscall
- [x] exit syscall
- [x] yield syscall

Phase 12: IPC & Microkernel Identity
- [x] Synchronous IPC (send/recv)
- [ ] Message copying
- [ ] Process naming or capability system
- [ ] Service registry

Phase 13: Program Loading
- [ ] Initrd/ramdisk loading
- [ ] ELF header parsing
- [ ] ELF segment loading
- [ ] User process bootstrap
- [ ] Init process (PID 1)

Phase 14: User Space
- [ ] Minimal libc (crt0.s)
- [ ] Syscall wrappers
- [ ] Shell
- [ ] Process spawning (fork/exec or spawn)
- [ ] Standard I/O

Phase 15: Networking
- [ ] NIC driver (LAN9118 or virtio-net)
- [ ] DMA buffer management
- [ ] Ethernet frame handling
- [ ] ARP table
- [ ] IPv4 packet handling
- [ ] ICMP (ping)
- [ ] UDP
- [ ] TCP
- [ ] Socket API (socket, bind, connect, send, recv)

Phase 16: Portability & HAL
- [ ] HAL interface: timer
- [ ] HAL interface: serial
- [ ] Clock speed from DTB
- [ ] HAL interface: IRQ controller
- [ ] Raspberry Pi port
- [ ] QEMU virt machine support
- [ ] Virtio drivers

Phase 17: Advanced Systems
- [ ] SMP boot (secondary cores)
- [ ] Spinlocks and atomic operations
- [ ] Block device driver
- [ ] Filesystem (FAT32 or ext2)
- [ ] Persistent storage
