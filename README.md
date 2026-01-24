

# Zuzu

Zuzu is a lightweight microkernel targeting AArch32 and ARMv7-A.

## Currently Supported Boards
- ARM Versatile Express `vexpress-a15`
- ... maybe Raspberry Pi 4
- ... maybe a RISC-V board
- ... something that definitely isn't x86

## Prerequisites
To build and run Zuzu on QEMU, install QEMU and `gcc-arm-none-eabi` cross-compiler:

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

## Building & Running
1. Building Zuzu

Compile Zuzu and generate kernel image:

```bash
make
```

2. Running:

Boot the kernel in QEMU using the included vexpress-v2p-ca15-tc1.dtb:

```bash
make run
```

Currently, support for other boards is minimal. This will hopefully change with the Raspberry Pi support.

3. Debugging

Start QEMU in suspended mode, listening on TCP port 1234 for GDB:

```bash
make debug
```

In another terminal, run:
```bash
gdb-multiarch build/zuzu.elf
(gdb) target remote :1234
```
## Structure
The Zuzu project follows this source structure for ease of development:

```
.
├── arch/
│   └── arm/
│       ├── vexpress-a15/  # Board-specific code (early setup, linker, board.h)
│       ├── mmu/           # Architecture-specific MMU logic
│       └── include/       # Generic ARM interfaces (irq.h)
├── kernel/
│   ├── dtb/               # Device Tree Parser
│   ├── mm/                # Memory Managers (PMM, Reserve)
│   └── init/              # Initialization logic
├── drivers/               # Hardware drivers (UART)
├── core/                  # Core utilities (kprintf, panic, assert)
└── lib/                   # Standard library implementations (memcpy, string)
```

## License
MIT License. See LICENSE for details.
