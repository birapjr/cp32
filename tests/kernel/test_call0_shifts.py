#!/usr/bin/env python3
"""Model the production leaf instructions for all defined 64-bit shift counts."""
from pathlib import Path
import random
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/kernel/klib32.S').read_text()
mask = (1 << 32) - 1

def execute(name, value, count):
    body = source.split(name + ':', 1)[1].split('.size', 1)[0]
    lines = [line.strip() for line in body.splitlines() if line.strip()]
    labels = {line[:-1]: i for i, line in enumerate(lines) if line.endswith(':')}
    regs = {'a2': value & mask, 'a3': value >> 32, 'a4': count}
    pc = 0
    sar = 0
    while True:
        line = lines[pc]
        pc += 1
        if line.endswith(':'):
            continue
        op, *args = line.replace(',', ' ').split()
        if op == 'ret':
            return (regs['a3'] << 32) | regs['a2']
        if op == 'beqz':
            if regs[args[0]] == 0:
                pc = labels[args[1][:-1]]
        elif op == 'bgei':
            if regs[args[0]] >= int(args[1]):
                pc = labels[args[2][:-1]]
        elif op == 'ssl':
            sar = 32 - (regs[args[0]] & 31)
        elif op == 'ssr':
            sar = regs[args[0]] & 31
        elif op == 'src':
            regs[args[0]] = (((regs[args[1]] << 32) | regs[args[2]]) >> sar) & mask
        elif op == 'sll':
            regs[args[0]] = (regs[args[1]] << (32 - sar)) & mask
        elif op == 'srl':
            regs[args[0]] = regs[args[1]] >> sar
        elif op == 'movi':
            regs[args[0]] = int(args[1])
        else:
            raise AssertionError('unmodeled instruction: ' + line)

rng = random.Random(174)
values = [0, 1, 0xffffffff, 0x100000000, 0xffffffffffffffff,
          0x8000000000000000] + [rng.getrandbits(64) for _ in range(100)]
for value in values:
    for count in range(64):
        assert execute('__ashldi3', value, count) == (value << count) & ((1 << 64) - 1)
        assert execute('__lshrdi3', value, count) == value >> count

# The ELF guard must reject a real windowed function and accept a call0 leaf.
with tempfile.TemporaryDirectory() as tmp:
    for instruction, expected in [('entry a1, 16\nretw', 1), ('ret', 0)]:
        asm = Path(tmp) / 'abi.S'
        obj = Path(tmp) / 'abi.o'
        asm.write_text('.text\n.global helper\n.type helper,@function\nhelper:\n' + instruction + '\n')
        subprocess.run(['xtensa-esp32s3-elf-gcc', '-c', str(asm), '-o', str(obj)], check=True)
        result = subprocess.run(['python3', str(root / 'tools/check_call0.py'), str(obj)], capture_output=True)
        assert result.returncode == expected, result.stderr
print('call0 shifts: all counts, boundary/random values and ELF ABI guard PASS')
