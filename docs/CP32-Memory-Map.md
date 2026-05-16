# CP32 Memoty Map

On this file I will document the memory map of the CP32 and the ESP32-S3.

![CP32 Memory Map](cp32-memory-map.png)

## make size

```text
.vectors    0    1077346304   ← Not yet defined: zero bytes
.startup   79    1077346304
.text    1840    1077346384
```
.vectors is empty (0 bytes). Your linker script has a .vectors section with KEEP(*(.vectors)) but nothing in your code emits a .vectors section. mpx32.S uses .startup.text — so there is no vector table at all yet. The _vectors_start and _vectors_end symbols both land at 0x40370000 which is why make nm shows them as ? (undefined size). This is fine for now since you haven't written ISR handlers yet, but wsr vecbase is pointing at empty memory.

```text
.data        4    0x3FC88000   ← g_magic (4 bytes) ✓
.rodata    344    0x3FC88004   ← strings from serial/wdt code ✓
.bss         4    0x3FC8815C   ← g_boot_count (4 bytes) ✓
.heap   131072    0x3FC88928   ← 128 KB reserved ✓
.stack   32768    0x3FCA8160   ← 32 KB ✓
```

All the data sections are correct and in the right order in DRAM.
Total RAM used: .data + .rodata + .bss + .heap + .stack = 4 + 344 + 4 + 131072 + 32768 = 164192 bytes = 160 KB out of 480 KB. You have 320 KB free DRAM.
Total IRAM used: .startup + .text = 79 + 1840 = 1919 bytes = ~1.9 KB out of 320 KB. Enormous room to grow.

## make segments — the critical one
```text
LOAD  VirtAddr=0x40370000  PhysAddr=0x42010000  FileSiz=0x780  MemSiz=0x780
LOAD  VirtAddr=0x3FC88000  PhysAddr=0x42010780  FileSiz=0x15C  MemSiz=0x20160
LOAD  VirtAddr=0x3FCA8160  PhysAddr=0x420108E0  FileSiz=0x000  MemSiz=0x08000
```

This is exactly right. Breaking it down:
Segment 0 — IRAM code:

```text
VMA 0x40370000 — where CPU executes it
LMA 0x42010000 — where esptool writes it in flash
FileSiz = MemSiz = 0x780 — no BSS-like expansion, pure code
```

Segment 1 — DRAM data:

```
VMA 0x3FC88000 — runtime address
LMA 0x42010780 — stored in flash right after the code segment
```
FileSiz=0x15C (348 bytes) vs MemSiz=0x20160 (131424 bytes) — the difference is .bss + .heap which take no flash space but are reserved in RAM. The ROM bootloader copies the 348 bytes and zeroes the rest.


Segment 2 — Stack:

```text
FileSiz=0x000 — takes no flash space at all, just reserves 32 KB in RAM
```
The ROM bootloader knows to reserve it but copies nothing

The _vectors issue confirmed:

Segment Sections...
```text
 00     .startup .text        ← .vectors missing! it's empty
 01     .data .rodata .bss .heap
 02     .stack
```
.vectors doesn't appear in any segment because it has zero size.

## make nm — symbols

```text
3fc88000  D  _data_start
3fc88000  d  g_magic          ← .data, 4 bytes, value 0xC0320000
3fc88004  D  _data_end
3fc8815c  B  _bss_start
3fc8815c  b  g_boot_count     ← .bss, 4 bytes, zeroed by startup
3fc88160  B  _bss_end
3fca8160  B  _stack_bottom
3fcb0160  B  _stack_top       ← a1 loaded here, stack is 32 KB ✓
40370000  ?  _vectors_end     ← same address as _vectors_start = empty
40370000  ?  _vectors_start
42010780  A  _data_lma        ← flash address startup copies .data from
```

Everything is where it should be. One thing worth noting — _data_lma is 0x42010780 which is _iram_lma + 0x780 (right after the code segment in flash), exactly matching the LMA in the segment dump.