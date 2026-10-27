#!/usr/bin/env python3
"""Read-only CP32 MINIX V2 consistency checker (14-byte names).

MINIX2 fsck responsibilities: reconstruct references and allocation, then compare
with disk metadata. No repair, writes, mounted-device access or journal replay.
Exit 0 clean, 1 inconsistent, 2 unsupported/invalid image or host I/O error.
"""
import argparse
import json
import mmap
from pathlib import Path
import struct


class InvalidImage(ValueError):
    pass


class Checker:
    def __init__(self, image):
        self.image = image
        self.start, self.length = 0, len(image)
        self.errors = []
        self.error_count = 0
        self.owners = {}
        self.nodes = {}
        self.directories = {}
        self.references = {}
        if len(image) < 2048:
            raise InvalidImage('image shorter than superblock')
        if image[510:512] == b'\x55\xaa':
            parts = [image[446+i*16:462+i*16] for i in range(4)]
            parts = [p for p in parts if p[4] == 0x81]
            if len(parts) != 1:
                raise InvalidImage('expected exactly one primary MINIX partition')
            p = parts[0]
            sector, count = struct.unpack_from('<II', p, 8)
            if p[0] not in (0, 128) or not sector or count < 4:
                raise InvalidImage('invalid MINIX partition geometry')
            self.start, self.length = sector*512, count*512
            if self.start+self.length > len(image):
                raise InvalidImage('partition exceeds image')
        raw = self.read(1024, 24)
        magic = raw[16:18]
        if magic not in (b'\x68\x24', b'\x24\x68'):
            raise InvalidImage('only MINIX V2 with 14-byte names is supported')
        self.order = '<' if magic == b'\x68\x24' else '>'
        (self.ninodes, _, self.imaps, self.zmaps, self.first, self.shift,
         self.max_size, _, _, self.zones) = struct.unpack(self.order+'6HIHHI', raw)
        if not self.ninodes or not self.imaps or not self.zmaps or self.shift > 4:
            raise InvalidImage('invalid or unsupported superblock geometry')
        self.zone_bytes = 1024 << self.shift
        self.inode_table = (2+self.imaps+self.zmaps)*1024
        if (self.zones*self.zone_bytes > self.length or self.first >= self.zones
                or self.inode_table+self.ninodes*64 > self.first*self.zone_bytes
                or self.ninodes+1 > self.imaps*8192
                or self.zones-self.first+1 > self.zmaps*8192):
            raise InvalidImage('superblock metadata or allocation maps exceed volume')

    def read(self, offset, size):
        if offset < 0 or size < 0 or offset+size > self.length:
            raise InvalidImage('read outside volume')
        return bytes(self.image[self.start+offset:self.start+offset+size])

    def error(self, text):
        self.error_count += 1
        if len(self.errors) < 1000:
            self.errors.append(text)

    def bit(self, offset, bit):
        word = struct.unpack(self.order+'H', self.read(offset+(bit//16)*2, 2))[0]
        return bool(word & (1 << (bit % 16)))

    def claim(self, zone, inode, kind):
        if not self.first <= zone < self.zones:
            self.error(f'inode {inode}: {kind} zone {zone} outside data area')
            return False
        if zone in self.owners:
            self.error(f'zone {zone}: duplicate owner {inode}/{kind}; first {self.owners[zone]}')
            return False
        self.owners[zone] = f'{inode}/{kind}'
        if not self.bit((2+self.imaps)*1024, zone-self.first+1):
            self.error(f'zone {zone}: referenced but bitmap marks free')
        return True

    def mapping(self, inode, size, zones):
        result = {}
        needed = (size+self.zone_bytes-1)//self.zone_bytes

        def data(zone, index):
            if not zone:
                return
            if index >= needed:
                self.error(f'inode {inode}: allocated data beyond EOF at index {index}')
            if self.claim(zone, inode, 'data'):
                result[index] = zone

        def table(zone, level, base):
            if not zone:
                return
            if base >= needed:
                self.error(f'inode {inode}: indirect table beyond EOF')
            if not self.claim(zone, inode, f'indirect{level}'):
                return
            entries = struct.unpack(self.order+'256I', self.read(zone*self.zone_bytes, 1024))
            for i, child in enumerate(entries):
                if level == 1:
                    data(child, base+i)
                else:
                    table(child, 1, base+i*256)
        for i, zone in enumerate(zones[:7]):
            data(zone, i)
        table(zones[7], 1, 7)
        table(zones[8], 2, 263)
        if zones[9]:
            self.error(f'inode {inode}: reserved tenth zone is nonzero')
        return result

    def run(self):
        imap, zmap = 2048, (2+self.imaps)*1024
        for name, offset in (('inode', imap), ('zone', zmap)):
            if not self.bit(offset, 0):
                self.error(f'{name} bitmap: reserved bit zero is free')
        for number in range(1, self.ninodes+1):
            if not self.bit(imap, number):
                continue  # freed inode bytes can legitimately retain stale data
            fields = struct.unpack(self.order+'4H4I10I', self.read(self.inode_table+(number-1)*64, 64))
            mode, links, _, _, size = fields[:5]
            kind = mode & 0o170000
            self.nodes[number] = (kind, links)
            if kind not in (0o100000, 0o040000):
                self.error(f'inode {number}: unsupported inode type {oct(kind)}')
                continue
            if not links:
                self.error(f'inode {number}: allocated with zero links (check only offline)')
            if size > self.max_size or size > (7+256+256*256)*self.zone_bytes:
                self.error(f'inode {number}: size exceeds mapping limit')
                continue
            mapping = self.mapping(number, size, fields[8:])
            if kind != 0o040000:
                continue  # regular files may contain sparse holes
            if size < 32 or size % 16:
                self.error(f'directory {number}: invalid size {size}')
                continue
            entries = {}
            needed = (size+self.zone_bytes-1)//self.zone_bytes
            if sum(i < needed for i in mapping) != needed:
                self.error(f'directory {number}: sparse/missing directory zones')
            for index in sorted(i for i in mapping if i < needed):
                raw = self.read(mapping[index]*self.zone_bytes, min(self.zone_bytes, size-index*self.zone_bytes))
                for pos in range(0, len(raw), 16):
                    child = struct.unpack_from(self.order+'H', raw, pos)[0]
                    if not child:
                        continue
                    name = raw[pos+2:pos+16].split(b'\0', 1)[0]
                    if not name or b'/' in name or any(c < 32 or c > 126 for c in name):
                        self.error(f'directory {number}: invalid name at {index*self.zone_bytes+pos}')
                    at = index*self.zone_bytes+pos
                    if (name == b'.' and at != 0) or (name == b'..' and at != 16):
                        self.error(f'directory {number}: misplaced dot entry at {at}')
                    if name in entries:
                        self.error(f'directory {number}: duplicate name {name!r}')
                    entries[name] = child
                    self.references[child] = self.references.get(child, 0)+1
                    if child > self.ninodes or not self.bit(imap, child):
                        self.error(f'directory {number}: entry {name!r} references free/invalid inode {child}')
            self.directories[number] = entries
        if 1 not in self.directories:
            self.error('root inode is not a valid allocated directory')
        seen, parents, todo = set(), {1: 1}, [1]
        while todo:
            node = todo.pop()
            if node in seen:
                continue
            seen.add(node)
            entries = self.directories.get(node)
            if entries is None:
                continue
            if entries.get(b'.') != node:
                self.error(f'directory {node}: incorrect dot entry')
            if entries.get(b'..') != parents[node]:
                self.error(f'directory {node}: incorrect dot-dot parent')
            for name, child in entries.items():
                if name in (b'.', b'..') or child not in self.nodes:
                    continue
                if child in self.directories:
                    if child in parents:
                        self.error(f'directory {child}: multiple parents or cycle')
                        continue
                    parents[child] = node
                todo.append(child)
        for node, (_, links) in self.nodes.items():
            if node not in seen:
                self.error(f'inode {node}: unreachable from root')
            actual = self.references.get(node, 0)
            if actual != links:
                self.error(f'inode {node}: link count {links}, directory references {actual}')
        for zone in range(self.first, self.zones):
            if self.bit(zmap, zone-self.first+1) and zone not in self.owners:
                self.error(f'zone {zone}: allocated but unreferenced')
        return {'clean': not self.error_count, 'errors': self.errors,
                'error_count': self.error_count, 'allocated_inodes': len(self.nodes),
                'referenced_zones': len(self.owners), 'partition_offset': self.start,
                'byte_order': 'little' if self.order == '<' else 'big'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path, help='offline regular-file image; never modified')
    parser.add_argument('--json', action='store_true', help='machine-readable report')
    args = parser.parse_args(argv)
    try:
        if not args.image.is_file():
            raise InvalidImage('input must be an offline regular-file image')
        with args.image.open('rb') as f:
            if f.seek(0, 2) < 2048:
                raise InvalidImage('image shorter than superblock')
            with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as image:
                report = Checker(image).run()
    except (OSError, ValueError) as exc:
        report = {'clean': False, 'error_count': 1, 'errors': [str(exc)]}
        status = 2
    else:
        status = 0 if report['clean'] else 1
    if args.json:
        print(json.dumps(report, sort_keys=True))
    else:
        print('MINIX V2: clean' if report['clean'] else 'MINIX V2: check failed')
        for error in report['errors']:
            print(error)
        if report['error_count'] > len(report['errors']):
            print(f"{report['error_count']-len(report['errors'])} further errors omitted")
    return status


if __name__ == '__main__':
    raise SystemExit(main())
