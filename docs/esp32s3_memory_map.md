# ESP32-S3 Memory Map

> Xtensa LX7 dual-core — 32-bit address space (4 GB total)

---

## Internal ROM

### ROM 0 — IROM (`0x40000000` – `0x4003FFFF`) · 256 KB
First-stage bootloader, ROM functions, and hardware abstraction routines burned at factory. Executable by the CPU but not writable.

- **Bus:** IRAM / IBUS
- **Access:** Read-only, Execute

### ROM 1 — DROM (`0x3FF00000` – `0x3FF1FFFF`) · 128 KB
Data section of the ROM. Holds ROM data tables, constant strings, and driver stubs accessible via the D-bus.

- **Bus:** DBUS
- **Access:** Read-only, Data

---

## Internal SRAM

### SRAM 0 — Instruction (`0x40370000` – `0x403BFFFF`) · 320 KB
High-speed internal SRAM exclusively mapped as instruction memory. Used for time-critical ISR code, code placed with `IRAM_ATTR`, and the CPU cache fill buffers.

- **Bus:** IBUS
- **Access:** Read/Write, Execute

### SRAM 1 — Data + Instruction (`0x3FC88000` – `0x3FCFFFFF`) · 480 KB
General-purpose internal SRAM. Simultaneously accessible as both data (DBUS) and instruction memory (IBUS alias at `0x40378000`). Largest on-chip scratchpad; holds heap, stack, `.bss`, and `.data`.

- **Bus:** DBUS / IBUS
- **Access:** Read/Write, Execute

### SRAM 2 — Data (`0x3FCF0000` – `0x3FCFFFFF`) · 64 KB
Additional data SRAM block. Accessible only via the DBUS; used as an extension of the main heap. May be powered down in light-sleep to save energy.

- **Bus:** DBUS
- **Access:** Read/Write, Low-power capable

---

## RTC Memory

### RTC FAST RAM (`0x600FE000` – `0x600FFFFF`) · 8 KB
Ultra-low-power SRAM that retains data across deep-sleep cycles. The RTC co-processor (ULP) and CPU 0 can access it. Mark variables with `RTC_FAST_ATTR` to preserve them across sleep.

- **Bus:** RTC bus
- **Access:** Retain in sleep, ULP access

### RTC SLOW RAM (`0x50000000` – `0x50001FFF`) · 8 KB
Accessible by the ULP co-processor in deep sleep without waking the main CPUs. Ideal for sensor data accumulation and wake-up condition storage.

- **Bus:** RTC SLOW bus
- **Access:** Retain in sleep, ULP only

---

## External Flash / PSRAM (via SPI Cache / MMU)

### Flash DROM — Cache (`0x3C000000` – `0x3DFFFFFF`) · Up to 32 MB
External SPI flash mapped read-only for data. The MMU + cache transparently fills from flash. `.rodata`, string literals, and WiFi firmware tables live here.

- **Bus:** Cache / MMU (DBUS)
- **Access:** Read-only, Cache-backed

### Flash IROM — Cache (`0x42000000` – `0x43FFFFFF`) · Up to 32 MB
External SPI flash mapped for instruction fetch via the I-cache. Application `.text` sections execute from here. Cache lines are 32 bytes; cache misses add ~several µs latency.

- **Bus:** Cache / MMU (IBUS)
- **Access:** Execute, Cache-backed

### PSRAM — Cache (`0x3D000000` – `0x3DFFFFFF`) · Up to 32 MB
Octal or quad SPI PSRAM (e.g. 8 MB) mapped into address space and cache-backed. Slower than internal SRAM (~80 ns vs ~2 ns) but useful for large buffers, frame buffers, and audio streams.

- **Bus:** Cache / SPI
- **Access:** Read/Write, Cache-backed, External

---

## Peripherals

### Peripheral Bus — APB (`0x60000000` – `0x600AFFFF`) · ~700 KB
Memory-mapped I/O for all on-chip peripherals: GPIO, UART, SPI, I2C, I2S, USB Serial JTAG, LEDC, MCPWM, ADC, RMT, TWAI (CAN), SHA, AES, RSA, and more. 32-bit word-aligned accesses only.

- **Bus:** APB
- **Access:** MMIO, Non-cacheable

### System & DMA Registers (`0x600C0000` – `0x600DFFFF`) · 128 KB
Interrupt matrix, DMA controller (GDMA), cache configuration, reset & clock control (RCC), and system timer registers.

- **Bus:** APB
- **Access:** MMIO, System control

---

## Special / Reserved

### ROM Aliases & Debug (`0x50002000` – `0x5FFFFFFF`)
Address ranges reserved by Espressif for future use, ROM aliases, and JTAG/OCD debug access. Reads return undefined values unless explicitly mapped.

- **Bus:** —
- **Access:** Reserved, Debug

---

## Quick Reference Table

| Region | Start | End | Size | Bus | R/W/X |
|---|---|---|---|---|---|
| ROM 0 (IROM) | `0x40000000` | `0x4003FFFF` | 256 KB | IBUS | R, X |
| ROM 1 (DROM) | `0x3FF00000` | `0x3FF1FFFF` | 128 KB | DBUS | R |
| SRAM 0 (Instr) | `0x40370000` | `0x403BFFFF` | 320 KB | IBUS | R/W, X |
| SRAM 1 (Data+Instr) | `0x3FC88000` | `0x3FCFFFFF` | 480 KB | DBUS/IBUS | R/W, X |
| SRAM 2 (Data) | `0x3FCF0000` | `0x3FCFFFFF` | 64 KB | DBUS | R/W |
| RTC FAST RAM | `0x600FE000` | `0x600FFFFF` | 8 KB | RTC | R/W |
| RTC SLOW RAM | `0x50000000` | `0x50001FFF` | 8 KB | RTC SLOW | R/W |
| Flash DROM (cache) | `0x3C000000` | `0x3DFFFFFF` | ≤ 32 MB | Cache/MMU | R |
| Flash IROM (cache) | `0x42000000` | `0x43FFFFFF` | ≤ 32 MB | Cache/MMU | R, X |
| PSRAM (cache) | `0x3D000000` | `0x3DFFFFFF` | ≤ 32 MB | Cache/SPI | R/W |
| Peripherals (APB) | `0x60000000` | `0x600AFFFF` | ~700 KB | APB | R/W |
| System & DMA regs | `0x600C0000` | `0x600DFFFF` | 128 KB | APB | R/W |
| Reserved / Debug | `0x50002000` | `0x5FFFFFFF` | — | — | — |

---

## Key Notes

- **`IRAM_ATTR`** — place time-critical functions (ISRs, tight loops) in internal SRAM 0 to avoid I-cache misses.
- **`RTC_FAST_ATTR` / `RTC_DATA_ATTR`** — variables survive deep sleep; useful for wake counters and state flags.
- **External flash execution** runs through a 2-way I-cache. Cache misses are the main latency source in flash-based apps.
- **PSRAM** is 30–40× slower than internal SRAM — avoid placing ISR code or frequently-polled buffers there.
- **All peripheral registers** are non-cacheable; always use `volatile` or the SDK register macros when accessing them.

---

*Reference: ESP32-S3 Technical Reference Manual, Espressif Systems.*
