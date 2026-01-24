# Zuzu Memory Map


This document describes the physical and virtual memory layout for the Zuzu kernel running on QEMU's `vexpress-a15` machine.

---

## Physical Memory Map

| Region               | Start        | End          | Size   | Description                        |
|----------------------|--------------|--------------|--------|------------------------------------|
| MMIO (CS3/IOFPGA)    | `0x1C000000` | `0x1FFFFFFF` | 64 MB  | Motherboard peripherals            |
| GIC Distributor      | `0x2C001000` | `0x2C001FFF` | 4 KB   | GICv2 distributor (GICD)           |
| GIC CPU Interface    | `0x2C002000` | `0x2C002FFF` | 4 KB   | GICv2 CPU interface (GICC)         |
| RAM                  | `0x80000000` | `0x8FFFFFFF` | 256 MB | Main system memory (QEMU: 64-256M) |

### Peripheral Addresses (relative to MMIO base `0x1C000000`)

| Device       | Offset       | Physical Address | IRQ  |
|--------------|--------------|------------------|------|
| UART0 (PL011)| `0x090000`   | `0x1C090000`     | 37   |
| UART1        | `0x0A0000`   | `0x1C0A0000`     | 38   |
| UART2        | `0x0B0000`   | `0x1C0B0000`     | 39   |
| UART3        | `0x0C0000`   | `0x1C0C0000`     | 40   |
| SP804 Timer  | `0x110000`   | `0x1C110000`     | 34,35|
| RTC (PL031)  | `0x170000`   | `0x1C170000`     | 36   |

---

## Virtual Memory Layout (Higher-Half Kernel)

After MMU enable, the kernel runs at virtual addresses starting at `0xC0000000`.

| Virtual Range                    | Physical Range                   | Size    | Type    | Description           |
|----------------------------------|----------------------------------|---------|---------|------------------------|
| `0x1C000000` - `0x1FFFFFFF`      | `0x1C000000` - `0x1FFFFFFF`      | 64 MB   | Device  | MMIO identity map      |
| `0x80000000` - `0x8FFFFFFF`      | `0x80000000` - `0x8FFFFFFF`      | 256 MB  | Normal  | RAM identity map (boot)|
| `0xC0000000` - `0xCFFFFFFF`      | `0x80000000` - `0x8FFFFFFF`      | 256 MB  | Normal  | Kernel higher-half     |

### Address Translation

```
VA (kernel)  = PA - 0x80000000 + 0xC0000000
             = PA + 0x40000000

PA           = VA - 0xC0000000 + 0x80000000
             = VA - 0x40000000
```

---

## Kernel Memory Layout (Physical)

Defined in `arch/arm/vexpress-a15/linker.ld`:

| Symbol              | Address        | Description                    |
|---------------------|----------------|--------------------------------|
| `KERNEL_PA_BASE`    | `0x80000000`   | Physical RAM base              |
| `BOOT_PA`           | `0x80010000`   | Boot code physical address     |
| `_boot_start`       | `0x80010000`   | Start of boot section          |
| `_boot_end`         | ~`0x80014000`  | End of boot section (4KB align)|
| `_kernel_phys_start`| `_boot_end`    | Start of main kernel (physical)|
| `_kernel_phys_end`  | varies         | End of main kernel (physical)  |

---

## Kernel Memory Layout (Virtual)

| Symbol              | Address        | Description                    |
|---------------------|----------------|--------------------------------|
| `KERNEL_VA_BASE`    | `0xC0000000`   | Virtual kernel base            |
| `_kernel_start`     | `0xC0004000`+  | Start of main kernel (virtual) |
| `_kernel_end`       | varies         | End of kernel (4KB aligned)    |

---

## Stack Layout (Physical)

Stacks grow downward. Allocated at physical addresses during early boot.

| Stack    | Top (SP init)  | Base           | Size   |
|----------|----------------|----------------|--------|
| SVC      | `0x80800000`   | `0x807F0000`   | 64 KB  |
| IRQ      | `0x807F0000`   | `0x807EC000`   | 16 KB  |
| ABT      | `0x807EC000`   | `0x807E4000`   | 32 KB  |
| UND      | `0x807E4000`   | `0x807E0000`   | 16 KB  |

Total stack region: 128 KB

---

## Page Table Format (ARMv7 Short-Descriptor)

### L1 Table
- 4096 entries x 4 bytes = 16 KB
- Must be 16 KB aligned
- Each entry maps 1 MB section

### L1 Section Entry (bits)
```
[31:20] Section base address (PA[31:20])
[19]    NS (Non-Secure)
[17]    nG (not Global)
[16]    S (Shareable)
[15]    AP[2]
[14:12] TEX[2:0]
[11:10] AP[1:0]
[8:5]   Domain
[4]     XN (Execute Never)
[3]     C (Cacheable)
[2]     B (Bufferable)
[1:0]   0b10 = Section descriptor
```

### Memory Attributes Used

| Type   | TEX | C | B | Description                    |
|--------|-----|---|---|--------------------------------|
| Normal | 001 | 1 | 1 | Write-Back, Write-Allocate     |
| Device | 000 | 0 | 1 | Shared Device (non-cacheable)  |

---

## DTB Location

- Loaded by QEMU at `0x80000000` (RAM base)
- Address passed in `r2` at boot
- Kernel preserves first 64 KB for DTB (`BOOT_OFFSET = 0x10000`)

---

## QEMU Invocation

```bash
qemu-system-arm \
    -M vexpress-a15 \
    -cpu cortex-a15 \
    -m 64M \
    -kernel build/zuzu.elf \
    -dtb arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb \
    -nographic
```

Debug mode (GDB):
```bash
qemu-system-arm \
    -M vexpress-a15 \
    -cpu cortex-a15 \
    -kernel build/zuzu.elf \
    -dtb arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb \
    -nographic \
    -S -s
```

Then connect with: `arm-none-eabi-gdb -x gdb.txt`