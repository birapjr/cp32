# ELF `.type` Directive for ESP32-S3 Assembly

The `.type` directive tells the assembler/linker **what kind of symbol** a label is.

```asm
.type _start, @function
```

| Part | Meaning |
|---|---|
| `.type` | The directive — sets the symbol type metadata in the ELF file |
| `_start` | The symbol name being described |
| `@function` | Declares it as a **function** (executable code, not data) |

---

## Why It Matters

It's purely metadata written into the ELF symbol table. It doesn't affect code generation, but it helps:

- **Debuggers (GDB)** — knows to treat the symbol as a function, enabling proper stack unwinding and call display
- **Linkers** — can make better decisions about dead code elimination and PLT/GOT entries
- **Tools like `objdump`, `nm`, `readelf`** — display it correctly as a function rather than an unknown symbol

---

## The Other `@` Types

| Type | Meaning |
|---|---|
| `@function` | Symbol is a function (executable code) |
| `@object` | Symbol is a data object (variable, array, struct) |
| `@notype` | No type specified (default if `.type` is omitted) |
| `@tls_object` | Thread-local storage variable |
| `@common` | Common block symbol (like uninitialized globals) |

---

## Typical Usage Pattern in ESP32-S3 Assembly

You'll almost always see `.type` paired with `.global` and a label:

```asm
    .global _start
    .type   _start, @function
_start:
    entry a1, 32
    ...
    retw
    .size _start, .-_start   ; optional: tells linker the function size
```

> `.size _start, .-_start` is a companion directive — the expression `.-_start` calculates the byte size of the function by subtracting the start address from the current position.
