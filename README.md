# zuzuMicrokernel

A minimalist, modular microkernel written for **ARM**, designed for experimentation, education, and fun.  
`zuzuMicrokernel` aims to be a clean foundation for building custom drivers, schedulers, and possibly a graphical environment in the future.

---
## Overview

`zuzuMicrokernel` is a small operating system kernel that focuses on:

- **Microkernel architecture** – minimal core (IPC, scheduling, memory)
- **ARM-based systems** – for learning low-level embedded and OS design
- **Modular drivers** – user-extendable components for devices and peripherals
- **Clean C + ASM design** – clarity and simplicity over bloat
- **Experimental goals** – future graphics layer, filesystem, and networking

This project is meant for an educational purpose, as an approachable path into **kernel development**, **low-level programming**, and **embedded system architecture**.

---

## Features (Planned)

| Category | Description | Status |
|-----------|--------------|--------|
| **Bootloader** | Basic ARM startup code | - |
| **Memory Management** | Simple physical memory allocator | - |
| **Task Scheduling** | Cooperative multitasking demo | - |
| **IPC Mechanism** | Message passing between tasks | - |
| **Drivers** | UART / Timer / GPIO | - |
| **Graphics Mode** | Basic framebuffer for output | - |
| **Virtual Memory** | Paging and MMU setup | - |

---

## Build Instructions

### Prerequisites

- `arm-none-eabi-gcc` (for cross-compiling)
- `make`
- `qemu-system-arm` (for emulation)
- `binutils` and `gdb` for debugging

### Building

```bash
git clone https://github.com/kgntmr/zuzuMicrokernel.git
cd zuzuMicrokernel
make
```

This will generate the starting object files and the zuzuMicrokernel.elf file in the `build` directory.

Running in QEMU

`qemu-system-arm -M versatilepb -kernel build/kernel.elf -serial stdio`

OR

Use make run


⸻


⸻

## Roadmap

1. Skeleton
2. UART Layer
3. Core Utilities
4. Memory & Task Structure
5. Timer & Preemption
6. Syscalls & IPC
7. User-space Servers
8. Network layer

⸻

## Learning Goals

This project aims to be a dive into how microkernel systems work.

⸻

# Contributing

I really appreciate contributions as I'm still learning this field.
If you’d like to contribute, you can do one of these:
	1.	Fork the repo
	2.	Submit a pull request
  3.  Report issues

You can also open issues for discussions or suggestions.

⸻

## License

GNU General Public License v3.0 (GPLv3)
Copyright © 2025 [Your Name]

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see https://www.gnu.org/licenses/￼.

⸻

Thanks so much to my little kitten Zuzu for the namesake.

---
