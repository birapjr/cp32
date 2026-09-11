#!/usr/bin/env python3
import re
import subprocess
import sys

def run(*args):
    return subprocess.check_output(args, text=True)

def symbols(path):
    out = run("xtensa-esp32s3-elf-nm", "-n", path)
    wanted = {"_iram_end", "_data_start", "_data_end", "_bss_start",
              "_bss_end", "_stack_bottom", "_stack_top", "cp32_data_sentinel",
              "test_ipc_mm"}
    result = {}
    for line in out.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] in wanted:
            result[fields[2]] = int(fields[0], 16)
    return result

def sections(path):
    out = run("xtensa-esp32s3-elf-readelf", "-SW", path)
    result = {}
    for line in out.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)", line, re.I)
        if m and m.group(1) in {".vectors", ".startup", ".text", ".data", ".rodata", ".bss"}:
            result[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return result

def report(path):
    print(path)
    for name, value in symbols(path).items():
        print(f"  {name:22} 0x{value:08x}")
    for name, (addr, size) in sections(path).items():
        print(f"  {name:22} addr=0x{addr:08x} size=0x{size:x}")
    print("  program headers:")
    for line in run("xtensa-esp32s3-elf-readelf", "-lW", path).splitlines():
        if re.match(r"\s*LOAD\s", line):
            print("   ", line.strip())

if len(sys.argv) not in (2, 3):
    raise SystemExit(f"usage: {sys.argv[0]} GOOD.elf [TEST.elf]")
report(sys.argv[1])
if len(sys.argv) == 3:
    report(sys.argv[2])
