#!/usr/bin/env python3
import re
import subprocess
import sys

elf, image = sys.argv[1:3]
text = subprocess.check_output(["xtensa-esp32s3-elf-readelf", "-lW", elf], text=True)
loads = []
for line in text.splitlines():
    m = re.match(r"\s*LOAD\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+.*\s+0x([0-9a-f]+)\s*$", line, re.I)
    if m:
        off, _, lma, filesz, _, align = (int(x, 16) for x in m.groups())
        loads.append((off, lma, filesz))
if len(loads) < 2:
    raise SystemExit("image layout check failed: no load segments")

sections = subprocess.check_output(["xtensa-esp32s3-elf-objdump", "-s", "-j", ".data", elf], text=True)
words = re.findall(r"^\s*[0-9a-f]+\s+([0-9a-f]{8})", sections, re.M | re.I)
if len(words) < 2:
    raise SystemExit("image layout check failed: .data sentinel unavailable")
sentinel = bytes.fromhex(words[1])
if sentinel not in open(image, "rb").read():
    raise SystemExit("image layout check failed: sentinel absent from image")

symbols = subprocess.check_output(["xtensa-esp32s3-elf-nm", "-n", elf], text=True)
iram_end = None
for line in symbols.splitlines():
    fields = line.split()
    if len(fields) == 3 and fields[2] == "_iram_end":
        iram_end = int(fields[0], 16)
        break
if iram_end is None:
    raise SystemExit("image layout check failed: _iram_end symbol unavailable")
iram_boundary = 0x40378000
if iram_end > iram_boundary:
    raise SystemExit(
        f"image layout check failed: IRAM end 0x{iram_end:08X} "
        f"crosses boundary 0x{iram_boundary:08X}"
    )
print(f"IRAM end: 0x{iram_end:08X}")
print(f"IRAM boundary: 0x{iram_boundary:08X}")
print(f"IRAM margin: {iram_boundary - iram_end} bytes")
print("image layout check: \033[32mPASS\033[0m")
