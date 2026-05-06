# zuzu Memory Map

This document describes the physical and virtual memory layout for the zuzu kernel on QEMU `vexpress-a15`.

---

## Physical Memory Map

| Region            | Start        | End          | Size  | Description              |
| ----------------- | ------------ | ------------ | ----- | ------------------------ |
| MMIO (CS3/IOFPGA) | `0x1C000000` | `0x1FFFFFFF` | 64 MB | Motherboard peripherals  |
| GIC Distributor   | `0x2C001000` | `0x2C001FFF` | 4 KB  | GICv2 distributor (GICD) |
| GIC CPU Interface | `0x2C002000` | `0x2C002FFF` | 4 KB  | GICv2 CPU interface      |
| RAM (default)     | `0x80000000` | `0x83FFFFFF` | 64 MB | QEMU default (`-m 64M`)  |

Notes:
- RAM size is configured by QEMU (`-m`) and discovered from DTB at boot.
- Example: with `-m 256M`, RAM range is `0x80000000` - `0x8FFFFFFF`.

### Peripheral Addresses (relative to MMIO base `0x1C000000`)

| Device        | Offset     | Physical Address | IRQ   |
| ------------- | ---------- | ---------------- | ----- |
| UART0 (PL011) | `0x090000` | `0x1C090000`     | 37    |
| UART1         | `0x0A0000` | `0x1C0A0000`     | 38    |
| UART2         | `0x0B0000` | `0x1C0B0000`     | 39    |
| UART3         | `0x0C0000` | `0x1C0C0000`     | 40    |
| SP804 Timer   | `0x110000` | `0x1C110000`     | 34,35 |
| RTC (PL031)   | `0x170000` | `0x1C170000`     | 36    |

---

## Virtual Memory Layout (Higher-Half Kernel)

After MMU enable, the kernel runs at virtual addresses starting at `0xC0000000`.

| Virtual Range               | Physical Range              | Size  | Type   | Description             |
| --------------------------- | --------------------------- | ----- | ------ | ----------------------- |
| `0x1C000000` - `0x1FFFFFFF` | `0x1C000000` - `0x1FFFFFFF` | 64 MB | Device | MMIO identity map       |
| `0x80000000` - `0x83FFFFFF` | `0x80000000` - `0x83FFFFFF` | 64 MB | Normal | RAM identity map (boot) |
| `0xC0000000` - `0xC3FFFFFF` | `0x80000000` - `0x83FFFFFF` | 64 MB | Normal | Kernel higher-half      |

For larger QEMU RAM sizes, identity and higher-half RAM windows expand accordingly.

### Address Translation

```
VA (kernel) = PA + 0x40000000
PA          = VA - 0x40000000
```

Constants:
- `KERNEL_PA_BASE = 0x80000000`
- `KERNEL_VA_BASE = 0xC0000000`
- `KERNEL_VA_OFFSET = 0x40000000`
- `USER_VA_TOP = 0x80000000` (TTBR0/TTBR1 split with `TTBCR.N = 1`)

---

## Kernel Memory Layout (Linker)

Defined in `arch/arm/vexpress-a15/linker.ld`:

| Symbol               | Address       | Description                                   |
| -------------------- | ------------- | --------------------------------------------- |
| `KERNEL_PA_BASE`     | `0x80000000`  | Physical RAM base                             |
| `BOOT_OFFSET`        | `0x10000`     | 64 KB reserved at RAM base (DTB window)       |
| `BOOT_PA`            | `0x80010000`  | Boot code physical address                    |
| `_boot_start`        | `0x80010000`  | Start of boot section                         |
| `_boot_end`          | varies        | End of boot section (page-aligned)            |
| `_kernel_start`      | computed      | `KERNEL_VA_BASE + (_boot_end - KERNEL_PA_BASE)` |
| `_kernel_end`        | varies        | End of kernel image (page-aligned)            |
| `_kernel_phys_start` | `_boot_end`   | Main kernel physical start                    |
| `_kernel_phys_end`   | varies        | Main kernel physical end                      |

---

## Stack Layout (Physical)

Stacks grow downward. Stack tops and sizes are defined by linker symbols.

| Stack | Top (SP init) | Base         | Size  |
| ----- | ------------- | ------------ | ----- |
| SVC   | `0x80800000`  | `0x807FE000` | 8 KB  |
| IRQ   | `0x807FE000`  | `0x807FA000` | 16 KB |
| ABT   | `0x807FA000`  | `0x807F9C00` | 1 KB  |
| UND   | `0x807F9C00`  | `0x807F9800` | 1 KB  |

Total stack region: 26 KB

---

## User Address Space Contract

Current user-space layout conventions:

| Virtual Range               | Size  | Purpose                    |
| --------------------------- | ----- | -------------------------- |
| `0x7FFFC000` - `0x80000000` | 16 KB | User stack (4 pages)       |
| `0x7FFFB000` - `0x7FFFC000` | 4 KB  | Stack guard page           |
| `0x7FFFA000` - `0x7FFFB000` | 4 KB  | IPCX shared syscall buffer |

---

## Page Table Format (ARMv7 Short-Descriptor)

### L1 Table
- 4096 entries x 4 bytes = 16 KB
- Must be 16 KB aligned
- Section entry maps 1 MB

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

| Type   | TEX | C   | B   | Description                   |
| ------ | --- | --- | --- | ----------------------------- |
| Normal | 001 | 1   | 1   | Write-back, write-allocate    |
| Device | 000 | 0   | 1   | Shared device (non-cacheable) |

---

## DTB Location

- Loaded by QEMU at `0x80000000` (RAM base)
- Address passed in `r2` at boot
- Kernel reserves first 64 KB for DTB and early data (`BOOT_OFFSET = 0x10000`)

---

## QEMU Invocation

Default run configuration (matches `make run`):

```bash
qemu-system-arm \
    -M vexpress-a15 \
    -cpu cortex-a15 \
    -m 64M \
    -kernel build/zuzu.elf \
    -dtb arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb \
    -nographic \
    -drive file=build/sd.img,if=sd,format=raw
```

Debug mode (matches `make debug`):

```bash
qemu-system-arm \
    -M vexpress-a15 \
    -cpu cortex-a15 \
    -m 64M \
    -kernel build/zuzu.elf \
    -dtb arch/arm/dtb/vexpress-a15/vexpress-v2p-ca15-tc1.dtb \
    -nographic \
    -S -gdb tcp::1234
```

Then connect with `arm-none-eabi-gdb -x gdb.txt`.