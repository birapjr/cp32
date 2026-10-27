#!/usr/bin/env python3
"""Wrap a MINIX V2 volume in one primary MBR partition; never access devices."""
import argparse
from pathlib import Path
import struct

def wrap(volume):
    if len(volume)<2048 or len(volume)%512 or volume[1040:1042] not in (b'\x68\x24', b'\x24\x68'):
        raise ValueError('expected a sector-aligned MINIX V2 volume')
    if len(volume)>0x7ffffe00:
        raise ValueError('volume exceeds CP32 signed byte-offset limit')
    image=bytearray(2048*512+len(volume))
    image[446:462]=struct.pack('<B3sB3sII',0,b'\xfe\xff\xff',0x81,b'\xfe\xff\xff',2048,len(volume)//512)
    image[510:512]=b'\x55\xaa'
    image[2048*512:]=volume
    return image

def expand(volume, size_mib=100, ninodes=4096):
    """Repack a clean V2 image into larger 1KiB-zone geometry, preserving inodes.

    This is an offline image operation, never an in-place card resize. The last
    physical block stays outside s_zones for CP32's restored-sector diagnostic.
    """
    from fsck_minix import Checker
    source = Checker(volume)
    source.run()
    if source.error_count or source.shift:
        raise ValueError('source must be a clean MINIX V2 image with 1KiB zones')
    if not 1 <= size_mib <= 1024 or not source.ninodes <= ninodes <= 65535:
        raise ValueError('size must be 1..1024 MiB; inode count must preserve source and fit 16 bits')
    size = size_mib * 1024 * 1024
    if size < source.length:
        raise ValueError('shrinking is not supported')
    zones = size // 1024 - 1
    imaps = (ninodes + 1 + 8191) // 8192
    # A conservative map covers even metadata zone numbers; valid data bits
    # are initialized below and all padding remains reserved.
    zmaps = (zones + 8191) // 8192
    first = 2 + imaps + zmaps + (ninodes + 15) // 16
    if first >= zones or first > 65535 or len(source.owners) > zones-first:
        raise ValueError('metadata and data do not fit requested geometry')
    result = bytearray(size)
    order = source.order
    struct.pack_into(order+'6HIHHI', result, 1024, ninodes, 0, imaps,
                     zmaps, first, 0, source.max_size, 0x2468, 0, zones)

    def bitmap(offset, blocks, usable, allocated):
        raw = bytearray(blocks * 1024)
        for bit in [0, *allocated, *range(usable+1, blocks*8192)]:
            pos = (bit//16)*2
            word = struct.unpack_from(order+'H', raw, pos)[0]
            struct.pack_into(order+'H', raw, pos, word | (1 << (bit%16)))
        result[offset:offset+len(raw)] = raw

    mapping = {old: first+i for i, old in enumerate(sorted(source.owners))}
    bitmap(2048, imaps, ninodes, source.nodes)
    bitmap((2+imaps)*1024, zmaps, zones-first,
           [zone-first+1 for zone in mapping.values()])
    for old, new in mapping.items():
        result[new*1024:(new+1)*1024] = source.read(old*1024, 1024)

    def relocate(zone, depth):
        if not zone:
            return 0
        target = mapping[zone]
        if depth:
            for i in range(256):
                child = struct.unpack(order+'I', source.read(zone*1024+i*4, 4))[0]
                struct.pack_into(order+'I', result, target*1024+i*4,
                                 relocate(child, depth-1))
        return target

    table = (2+imaps+zmaps)*1024
    for number in source.nodes:
        fields = list(struct.unpack(order+'4H4I10I',
                      source.read(source.inode_table+(number-1)*64, 64)))
        for i in range(9):
            fields[8+i] = relocate(fields[8+i], max(0, i-6))
        struct.pack_into(order+'4H4I10I', result, table+(number-1)*64, *fields)
    check = Checker(result)
    check.run()
    if check.error_count:
        raise ValueError('expanded image failed consistency check: '+check.errors[0])
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('volume',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--size-mib', type=int, help='repack into this volume size')
    parser.add_argument('--inodes', type=int, default=4096)
    args=parser.parse_args()
    volume=args.volume.read_bytes()
    if args.size_mib is not None:volume=expand(volume,args.size_mib,args.inodes)
    image=wrap(volume)
    # Exclusive creation avoids following an existing device node or symlink.
    # Permit reproducible rebuilds of regular files, never special files.
    if args.output.is_symlink() or (args.output.exists() and not args.output.is_file()):
        parser.error('output must be a regular image file')
    if args.output.exists():args.output.unlink()
    with args.output.open('xb') as f:f.write(image)
    print(f'{args.output}: {len(image)} bytes, MINIX partition at sector 2048')
if __name__=='__main__':main()
