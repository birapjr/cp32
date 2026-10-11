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

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('volume',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    image=wrap(args.volume.read_bytes())
    # Exclusive creation avoids following an existing device node or symlink.
    # Permit reproducible rebuilds of regular files, never special files.
    if args.output.is_symlink() or (args.output.exists() and not args.output.is_file()):
        parser.error('output must be a regular image file')
    if args.output.exists():args.output.unlink()
    with args.output.open('xb') as f:f.write(image)
    print(f'{args.output}: {len(image)} bytes, MINIX partition at sector 2048')
if __name__=='__main__':main()
