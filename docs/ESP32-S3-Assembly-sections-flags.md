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

