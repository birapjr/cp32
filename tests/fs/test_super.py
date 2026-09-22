#!/usr/bin/env python3
"""Exercise the compiled production decoder with serialized MINIX headers."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
class Super(C.Structure):
    _fields_ = [(name, C.c_uint) for name in (
        'ninodes', 'zones', 'first_data_zone', 'log_zone_size', 'max_size',
        'imap_blocks', 'zmap_blocks', 'swapped')]
Reader = C.CFUNCTYPE(C.c_int, C.c_uint, C.c_void_p, C.c_int)

with tempfile.TemporaryDirectory() as tmp:
    libpath = str(Path(tmp) / 'super.so')
    subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'src/kernel'),
                    str(ROOT/'src/fs/super.c'), str(ROOT/'src/fs/utility.c'), '-o', libpath], check=True)
    lib = C.CDLL(libpath)
    lib.cp32_minix_super_read.argtypes = [Reader, C.c_uint, C.POINTER(Super)]

    def check(expected=0, endian='<', capacity=65536, returned=24, **changes):
        fields = dict(ninodes=32, old_zones=0, imap=1, zmap=1, first=6,
                      shift=0, max_size=0x7fffffff, magic=0x2468, pad=0, zones=64)
        fields.update(changes)
        data = struct.pack(endian+'6HIHHI', *fields.values())
        requests = []
        @Reader
        def read(offset, buffer, count):
            requests.append((offset, count))
            assert (offset, count) == (1024, 24)
            C.memmove(buffer, data, 24)
            return returned
        out = Super(*([0xa5a5a5a5]*8))
        before = bytes(out)
        result = lib.cp32_minix_super_read(read, capacity, C.byref(out))
        assert result == expected, (result, expected, fields)
        assert len(requests) == (0 if capacity < 2048 else 1)
        if result:
            assert bytes(out) == before, 'Failure published partial geometry'
        else:
            assert out.ninodes == fields['ninodes'] and out.zones == fields['zones']
            assert out.swapped == (endian == '>')
        return out

    for endian in ('<', '>'):
        check(endian=endian)
        check(endian=endian, shift=1, first=3, zones=32)
        check(endian=endian, shift=4, first=1, zones=4)
        check(2, endian=endian, magic=0x137f)
        check(1, endian=endian, magic=0)
        for bad in (dict(ninodes=0), dict(imap=0), dict(zmap=0),
                    dict(imap=65535), dict(zmap=65535), dict(shift=5),
                    dict(shift=65535), dict(max_size=0), dict(max_size=0xffffffff),
                    dict(first=5), dict(first=64), dict(zones=0),
                    dict(zones=65), dict(zones=0xffffffff),
                    dict(ninodes=8192, first=520, zones=600, capacity=614400),
                    dict(zones=9000, capacity=9216000)):
            check(3, endian=endian, **bad)
    check(3, capacity=1024)
    for result in (-5, 0, 23, 25):
        check(4, returned=result)
    print('MINIX V2 superblock: endian, bounds, maps, overflow and short I/O pass')
