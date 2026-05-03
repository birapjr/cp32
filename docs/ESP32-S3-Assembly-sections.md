# ESP32-S3 Assembly Section Names Reference

## Standard ELF Sections (Xtensa / GNU Toolchain)

These are the baseline sections from the Xtensa assembler, familiar from most ELF targets:

| Section | Description |
|---|---|
| `.text` | Executable code (instructions) |
| `.literal` | **Xtensa-specific.** Literal pool — constants that are too large to encode inline in instructions (addresses, 32-bit immediates). The Xtensa ISA loads literals via `L32R`, so they live near code in this section. |
| `.rodata` | Read-only data (const globals, string literals) |
| `.data` | Initialized read-write data (global/static vars with initial values) |
| `.bss` | Uninitialized read-write data (zero-initialized globals) |
| `.noinit` | Data that should not be initialized at startup at all |

> **`.literal` is the big Xtensa-specific one** — unlike ARM or x86, the Xtensa LX7 always loads 32-bit constants from a nearby literal pool using the `L32R` instruction, so you'll always see `.literal` alongside `.text`.

---

## ESP-IDF / ESP32-S3 Memory-Specific Sections

The ESP32-S3 has several distinct memory regions, and ESP-IDF uses special section names to route code/data into the right one:

| Section | Memory Region | Description |
|---|---|---|
| `.iram1` | IRAM (Internal RAM) | Code that must run from RAM — faster, works during flash cache misses, required for interrupt handlers |
| `.iram1.text` | IRAM | Text (code) explicitly placed in IRAM |
| `.dram0.data` | DRAM | Data placed in data RAM explicitly |
| `.dram0.bss` | DRAM | BSS (zero-init) in data RAM |
| `.flash.text` | Flash (cached) | Default location for code — slower than IRAM but saves RAM |
| `.flash.rodata` | Flash (cached) | Default for read-only data stored in flash |
| `.rtc.text` | RTC fast memory | Code that runs during deep sleep wake stubs |
| `.rtc.data` | RTC slow memory | Data retained during deep sleep |
| `.rtc.bss` | RTC slow memory | Zero-init data in RTC slow memory |
| `.rtc_noinit` | RTC slow memory | Data kept across resets, never re-initialized |

---

## Linker Output Sections

These are the final output sections the linker produces, combining the above:

| Output Section | Fed by |
|---|---|
| `.iram0.text` | `.iram1`, `.iram1.*`, plus any noflash-mapped `.text`/`.literal` |
| `.dram0.data` | `.data`, `.dram0.data`, plus noflash-mapped `.rodata` |
| `.dram0.bss` | `.bss`, `.dram0.bss` |
| `.flash.text` | Default `.text`, `.literal` (cached flash) |
| `.flash.rodata` | Default `.rodata` (cached flash) |
| `.rtc.text` | `.rtc.text` |
| `.rtc.data` / `.rtc.bss` | RTC memory sections |

---

## Example: Using Sections in Xtensa Assembly

```asm
    .section .iram1, "ax"   ; force this code into IRAM
    .align 4
my_isr:
    ...

    .section .text           ; normal code (goes to flash by default)
    .align 4
my_func:
    entry a1, 32
    ...
    retw

    .section .literal        ; literal pool (Xtensa-specific)
    .align 4
.LC0:
    .word 0x3FF44000         ; some peripheral base address

    .section .rodata         ; read-only data
    .align 4
my_string:
    .asciz "Hello ESP32-S3"

    .section .data           ; initialized data
    .align 4
my_var:
    .word 42

    .section .bss            ; zero-initialized data
    .align 4
my_buf:
    .space 64
```

---

## Key ESP32-S3-Specific Notes

- **`.literal` is mandatory** — whenever you use `L32R` (which the compiler does for any 32-bit constant), the assembler emits a `.literal` section. You rarely write it by hand, but you'll always see it in disassembly.
- **IRAM vs Flash**: Code in `.text` (flash) can stall during flash cache misses (e.g., during SPI flash writes). Interrupt handlers and timing-critical code should use `.iram1` / `IRAM_ATTR` in C.
- **RTC sections** are for code/data that must survive deep sleep, like wake stubs.
- In ESP-IDF C code, the attributes `IRAM_ATTR`, `DRAM_ATTR`, and `RTC_IRAM_ATTR` map to these sections via `__attribute__((section(...)))`.

# ELF Section Flags for ESP32-S3 Assembly

The flags string (e.g. `"ax"`) at the end of a `.section` directive tells the assembler/linker what properties that section has.

```asm
.section .text, "ax"
```

Each letter is a flag:

| Flag | Meaning |
|---|---|
| `a` | **Allocatable** — the section occupies memory at runtime (gets loaded into the chip) |
| `x` | **Executable** — the section contains executable instructions |
| `w` | **Writable** — the section can be written to at runtime |

So `.section .text, "ax"` means: *"this section is loaded into memory AND contains executable code"* — which is exactly what a code section should be.

---

## Common Flag Combinations on ESP32-S3

| Flags | Typical Section | Meaning |
|---|---|---|
| `"ax"` | `.text`, `.iram1` | Allocatable + Executable (code) |
| `"aw"` | `.data`, `.dram0.data` | Allocatable + Writable (read-write data) |
| `"a"` | `.rodata` | Allocatable only (read-only data) |
| `""` or no flags | `.comment`, debug sections | Not loaded at runtime |

---

## Full Flag Alphabet

| Flag | Meaning |
|---|---|
| `a` | Allocatable |
| `x` | Executable |
| `w` | Writable |
| `m` | Mergeable (linker may merge identical entries) |
| `s` | Contains null-terminated strings |
| `g` | Section group member |

> `a`, `x`, and `w` are the three you'll use 99% of the time in bare-metal ESP32-S3 assembly.

