# Zuzu Kernel Roadmap

## Phase 1: Bootstrap 
- [x] Boot assembly (_start.s): stack init, BSS zero, branch to C
- [x] PL011 UART driver for early console
- [x] String and memory utils (memcpy, memset, strlen)
- [x] Simple kprintf implementation
- [x] kprintf/kpanic logging infrastructure
- [x] Linker script for higher-half kernel mapping
- [x] DTB parser for hardware discovery

## Phase 2: Memory Management 
- [x] ARMv7 first level page table setup
- [x] Bitmap-based physical page allocator (PMM)
- [x] MMU enable with identity + higher-half mapping
- [x] Linked list kernel heap allocator (kmalloc/kfree)
- [x] ARMv7 second level page table setup


## Phase 3: Interrupts 
- [ ] GICv2 distributor + CPU interface init
- [ ] SP804 periodic tick (scheduler heartbeat)
- [x] Context save/restore stubs (entry.s)
- [x] IRQ dispatch to registered handlers
- [x] Exception vector table (vectors.s)

## Phase 4: Processes
- [ ] Process Control Block (PCB) structure
- [ ] Context switch (register save/restore)
- [ ] Round-robin scheduler
- [ ] SVC mode to USR mode drop
- [ ] System call entry (SVC handler)

## Phase 5: IPC
- [ ] Synchronous message passing (send/recv)
- [ ] Process naming or capability system
- [ ] Basic service registry

## Phase 6: User Space
- [ ] ELF loader
- [ ] Ramdisk/initrd support
- [ ] Minimal libc (crt0, syscall wrappers)
- [ ] Init process and shell

## Phase 7: Networking
- [ ] NIC driver (LAN9118 or virtio-net)
- [ ] Ethernet frame handling
- [ ] ARP table
- [ ] IPv4 + ICMP (ping)
- [ ] UDP, then TCP
- [ ] Socket syscalls

## Phase 8: Portability
- [ ] HAL: abstract timer, serial, IRQ controller
- [ ] Raspberry Pi port
- [ ] QEMU virt + virtio drivers

## Phase 9: Advanced
- [ ] SMP boot and spinlocks
- [ ] Block device + filesystem (FAT32/ext2)
