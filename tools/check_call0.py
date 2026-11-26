#!/usr/bin/env python3
"""Check call0 entry and, for the kernel, instructions executed with EXCM set."""
import argparse
import re
import subprocess


def check(elf, kernel=False):
    if kernel:
        sections = subprocess.check_output(
            ['xtensa-esp32s3-elf-readelf', '-SW', elf], text=True)
        if not re.search(r'\]\s+\.xt\.prop\s+PROGBITS\b', sections):
            raise SystemExit('kernel loop check requires .xt.prop for disassembly')
    symbols = subprocess.check_output(
        ['xtensa-esp32s3-elf-readelf', '-sW', elf], text=True)
    functions = {}
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[3] == 'FUNC' and fields[6] != 'UND':
            functions[int(fields[1], 16)] = fields[7]
    asm = subprocess.check_output(
        ['xtensa-esp32s3-elf-objdump', '-d', elf], text=True)
    location = ''
    for line in asm.splitlines():
        if kernel:
            label = re.match(r'^[0-9a-f]+ <(.+)>:', line)
            if label:
                location = label[1]
            # -d skips non-executable sections. The linked .xt.prop records
            # keep literal words out of the instruction decoder in text too.
            loop = re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+(loop(?:nez|gtz)?)\s', line)
            if loop:
                raise SystemExit(f'kernel loop check failed: {loop[2]} at '
                                 f'0x{loop[1]} in {location}')
        match = re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]+\s+entry\s', line)
        if match and int(match[1], 16) in functions:
            raise SystemExit('call0 ABI check failed: windowed function ' +
                             functions[int(match[1], 16)])
    print('call0 ABI check: PASS')
    if kernel:
        print('kernel exception-mode loop check: PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', action='store_true', help='also check for LX7 hardware loops')
    parser.add_argument('elf')
    args = parser.parse_args()
    check(args.elf, kernel=args.kernel)
