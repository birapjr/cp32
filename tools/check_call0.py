#!/usr/bin/env python3
"""Reject windowed function prologues in a call0 ELF (ignore literal pools)."""
import re
import subprocess
import sys


def check(elf):
    symbols = subprocess.check_output(
        ['xtensa-esp32s3-elf-readelf', '-sW', elf], text=True)
    functions = {}
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[3] == 'FUNC' and fields[6] != 'UND':
            functions[int(fields[1], 16)] = fields[7]
    asm = subprocess.check_output(
        ['xtensa-esp32s3-elf-objdump', '-d', elf], text=True)
    for line in asm.splitlines():
        match = re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+entry\s', line)
        if match and int(match[1], 16) in functions:
            raise SystemExit('call0 ABI check failed: windowed function ' +
                             functions[int(match[1], 16)])
    print('call0 ABI check: PASS')


if __name__ == '__main__':
    check(sys.argv[1])
