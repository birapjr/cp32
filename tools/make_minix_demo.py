#!/usr/bin/env python3
"""Deterministic original MINIX V2 image; no host mkfs dependency.

Reference: minix-2.0.0/src/fs/{super.h,type.h,const.h}. Block/zone size
1024, 32 inodes, 14-byte names. Physical final block is outside s_zones
and remains reserved for CP32's restored-sector device diagnostic.
"""
import argparse
from pathlib import Path
import struct

README = b'CP32 MINIX V2 RAM filesystem.\nRead-only filesystem bring-up.\n'

# Exactly seven direct zones, then a short single-indirect tail.
INDIRECT = b''.join(
    (f'Direct zone {zone+1}, line {line+1:02d}'.ljust(31)+'\n').encode('ascii')
    for zone in range(7) for line in range(32)) + b'INDIRECT READ OK\n'

DOUBLE = b''.join(f'Double indirect line {i:02d}\n'.encode() for i in range(1,11)) + b'DOUBLE INDIRECT READ OK\n'

def build_image(include_indirect=False, hello=None, echo=None, cat=None, wc=None, ls=None):
    """Keep a minimal parser fixture; CLI firmware always includes INDIRECT."""
    disk = bytearray(65536)
    disk[1024:1048] = struct.pack('<6HIHHI', 32, 0, 1, 1, 6, 0,
                                  0x7fffffff, 0x2468, 0, 63)
    # Reserved bit zero, three inodes/zones in use, and all unavailable bits.
    for offset, usable in ((2048, 32), (3072, 57)):
        bitmap = bytearray(b'\xff' * 1024)
        for bit in range(4, usable + 1):
            bitmap[bit // 8] &= ~(1 << (bit % 8))
        disk[offset:offset+1024] = bitmap

    def inode(number, mode, links, size, zone):
        raw = struct.pack('<4H4I10I', mode, links, 0, 0, size, 0, 0, 0,
                          zone, *([0] * 9))
        offset = 4096 + (number-1)*64
        disk[offset:offset+64] = raw

    def directory(zone, entries):
        for index, (number, name) in enumerate(entries):
            raw = struct.pack('<H14s', number, name.encode('ascii'))
            offset = zone*1024 + index*16
            disk[offset:offset+16] = raw

    inode(1, 0o40755, 3, 64, 6)
    inode(2, 0o40755, 2, 48, 7)
    inode(3, 0o100444, 2, len(README), 8)
    directory(6, [(1, '.'), (1, '..'), (2, 'boot'), (3, 'readme')])
    directory(7, [(2, '.'), (1, '..'), (3, 'readme')])
    disk[8192:8192+len(README)] = README
    if include_indirect:
        # Inode 4, direct data zones 9..15, indirect table 16, tail zone 17.
        inode(2, 0o40755, 3, 96, 7)
        directory(7, [(2, '.'), (1, '..'), (3, 'readme'), (4, 'indirect'), (5, 'double'), (6, 'large')])
        disk[4288:4352] = struct.pack('<4H4I10I', 0o100444, 1, 0, 0,
            len(INDIRECT), 0, 0, 0, *range(9, 16), 16, 0, 0)
        disk[2048] |= 1 << 4
        for zone in range(9, 18):
            bit = zone - 6 + 1
            disk[3072 + bit//8] |= 1 << (bit%8)
        disk[9*1024:16*1024] = INDIRECT[:7*1024]
        struct.pack_into('<I', disk, 16*1024, 17)
        disk[17*1024:17*1024+len(INDIRECT)-7*1024] = INDIRECT[7*1024:]
        # Sparse logical file: double-indirect data begins at zone index 263.
        disk[4352:4416] = struct.pack('<4H4I10I', 0o100444, 1, 0, 0,
            263*1024+len(DOUBLE), 0, 0, 0, *([0]*8), 18, 0)
        disk[2048] |= 1 << 5
        for zone in range(18, 21):
            bit = zone - 5
            disk[3072+bit//8] |= 1 << (bit%8)
        struct.pack_into('<I', disk, 18*1024, 19)
        struct.pack_into('<I', disk, 19*1024, 20)
        disk[20*1024:20*1024+len(DOUBLE)] = DOUBLE
        # Directory with deleted slots and a live entry beyond seven zones.
        inode(3, 0o100444, 3, len(README), 8)
        disk[4416:4480] = struct.pack('<4H4I10I',0o40755,2,0,0,
            7*1024+16,0,0,0,*range(21,28),28,0,0)
        disk[2048] |= 1 << 6
        for zone in range(21,30):
            bit=zone-5
            disk[3072+bit//8] |= 1 << (bit%8)
        directory(21,[(6,'.'),(2,'..')])
        struct.pack_into('<I',disk,28*1024,29)
        directory(29,[(3,'readme')])
    entries=[(2,'.'),(1,'..'),(3,'readme'),(4,'indirect'),(5,'double'),(6,'large')]
    next_zone = 30
    for number,name,payload in ((7,'hello',hello),(8,'echo',echo),(9,'cat',cat),(10,'wc',wc),(11,'ls',ls)):
        if payload is None:
            continue
        if not include_indirect or not payload:
            raise ValueError('application requires full fixture and nonempty payload')
        count = (len(payload)+1023)//1024
        # MINIX V2: seven direct zones, followed by 32-bit indirect entries.
        # This small disk needs at most one single-indirect table per file.
        required = count + (count > 7)
        if next_zone + required > 63:
            raise ValueError('applications exceed available MINIX image zones')
        zones = list(range(next_zone, next_zone+count))
        table = next_zone+count if count > 7 else 0
        pointers = zones[:7] + [0] * max(0, 7-count) + [table, 0, 0]
        if table:
            for index, zone in enumerate(zones[7:]):
                struct.pack_into('<I', disk, table*1024+index*4, zone)
        entries.append((number,name))
        inode(2,0o40755,3,len(entries)*16,7)
        directory(7,entries)
        offset=4096+(number-1)*64
        disk[offset:offset+64]=struct.pack('<4H4I10I',0o100555,1,0,0,
            len(payload),0,0,0,*pointers)
        disk[2048+number//8] |= 1<<(number%8)
        for zone in range(next_zone,next_zone+required):
            bit=zone-5
            disk[3072+bit//8] |= 1<<(bit%8)
        disk[next_zone*1024:next_zone*1024+len(payload)]=payload
        next_zone += required
    return bytes(disk)

def encode_runs(disk):
    """Store nonzero runs; a zeroed destination reconstructs exact disk bytes."""
    runs=[]
    i=0
    while i<len(disk):
        if disk[i]==0:
            i+=1
            continue
        start=i
        while i<len(disk) and disk[i]!=0:
            i+=1
        # Each run costs four metadata bytes. Include short zero gaps when
        # cheaper than another record; reconstructed disk bytes are identical.
        if runs and start-(runs[-1][0]+len(runs[-1][1]))<=4:
            previous,_=runs.pop()
            runs.append((previous,disk[previous:i]))
        else:
            runs.append((start,disk[start:i]))
    return runs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', required=True)
    parser.add_argument('--header', required=True)
    parser.add_argument('--hello')
    parser.add_argument('--echo')
    parser.add_argument('--cat')
    parser.add_argument('--wc')
    parser.add_argument('--ls')
    args = parser.parse_args()
    disk = build_image(include_indirect=True,
                       hello=Path(args.hello).read_bytes() if args.hello else None,
                       echo=Path(args.echo).read_bytes() if args.echo else None,
                       cat=Path(args.cat).read_bytes() if args.cat else None,
                       wc=Path(args.wc).read_bytes() if args.wc else None,
                       ls=Path(args.ls).read_bytes() if args.ls else None)
    Path(args.image).write_bytes(disk)
    # Offset/length records plus packed data avoid firmware-sized zero gaps.
    runs=encode_runs(disk)
    payload=b''.join(data for _,data in runs)
    lines=['/* Generated by tools/make_minix_demo.py; do not edit. */',
           'static const unsigned char cp32_demo_bytes[] = {']
    for offset in range(0,len(payload),16):
        lines.append('  '+','.join(str(x) for x in payload[offset:offset+16])+',')
    lines.append('};')
    lines.append('static const unsigned short cp32_demo_runs[][2] = {')
    for offset,data in runs:
        lines.append('  {'+str(offset)+','+str(len(data))+'},')
    lines.append('};\n')
    Path(args.header).write_text('\n'.join(lines))

if __name__ == '__main__':
    main()
