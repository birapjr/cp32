#!/usr/bin/env python3
"""Production directory decoder with valid and hostile disk images."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from make_minix_demo import build_image, README, INDIRECT, DOUBLE

class Super(C.Structure):
    _fields_ = [(name, C.c_uint) for name in (
        'ninodes', 'zones', 'first', 'shift', 'maximum', 'imap', 'zmap', 'swap')]
Reader = C.CFUNCTYPE(C.c_int, C.c_uint, C.c_void_p, C.c_int)
class Dir(C.Structure):
    _fields_ = [('super', Super), ('read', Reader), ('size', C.c_uint),
                ('position', C.c_uint), ('zones', C.c_uint*7), ('indirect', C.c_uint)]
class File(C.Structure):
    _fields_ = [('read', Reader), ('size', C.c_uint), ('position', C.c_uint),
                ('zone_bytes', C.c_uint), ('zones', C.c_uint*7),
                ('super', Super), ('indirect', C.c_uint), ('double_indirect', C.c_uint), ('inode', C.c_uint)]

def swapped_image(original):
    data = bytearray(original)
    for offset, fmt in [(1024, '6HIHHI')] + [(4096+i*64, '4H4I10I') for i in range(3)]:
        size = struct.calcsize('<'+fmt)
        data[offset:offset+size] = struct.pack('>'+fmt, *struct.unpack_from('<'+fmt, original, offset))
    for offset in range(2048, 4096, 2):
        data[offset:offset+2] = original[offset:offset+2][::-1]
    for offset in list(range(6144, 6208, 16)) + list(range(7168, 7216, 16)):
        data[offset:offset+2] = original[offset:offset+2][::-1]
    return data

with tempfile.TemporaryDirectory() as tmp:
    path = str(Path(tmp)/'dir.so')
    subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'src/kernel'),
                    str(ROOT/'src/fs/super.c'), str(ROOT/'src/fs/utility.c'),
                    *[str(ROOT/'src/fs'/name) for name in ('inode.c','path.c','misc.c','filedes.c','open.c','read.c','stadir.c')], '-o', path], check=True)
    lib = C.CDLL(path)
    lib.cp32_minix_root_open.argtypes = [Reader, C.c_uint, C.POINTER(Dir)]
    lib.cp32_minix_dir_open.argtypes = [Reader, C.c_uint, C.c_char_p, C.POINTER(Dir)]
    lib.cp32_minix_root_next.argtypes = [C.POINTER(Dir), C.POINTER(C.c_uint), C.c_void_p]
    lib.cp32_minix_file_open.argtypes = [Reader, C.c_uint, C.c_char_p, C.POINTER(File)]
    lib.cp32_minix_file_read.argtypes = [C.POINTER(File), C.c_void_p, C.c_uint]
    image = build_image()
    assert image == build_image() and len(image) == 65536
    assert image[8192:8192+len(README)] == README
    assert not any(image[63*1024:]), 'Scratch block must be outside the filesystem'
    assert struct.unpack_from('<I', image, 1044)[0] == 63
    # Independently verify the fixture's inode links and bitmap ownership.
    links = {1: 0, 2: 0, 3: 0}
    for base, size in ((6144, 64), (7168, 48)):
        for offset in range(base, base+size, 16):
            links[struct.unpack_from('<H', image, offset)[0]] += 1
    for number in range(1, 4):
        assert struct.unpack_from('<H', image, 4096+(number-1)*64+2)[0] == links[number]
        assert image[2048+number//8] & (1 << (number%8))
    for zone in range(6, 63):
        bit = zone-6+1
        assert bool(image[3072+bit//8] & (1 << (bit%8))) == (zone in (6, 7, 8))

    def run(data, expected=0, expected_names=None, fail_at=None, short=False, path=None):
        snapshot = bytes(data)
        calls = []
        @Reader
        def read(offset, buffer, count):
            assert 0 <= offset <= len(data) and 0 < count <= len(data)-offset
            calls.append((offset, count))
            if len(calls) == fail_at:
                return count-1 if short else -5
            C.memmove(buffer, bytes(data[offset:offset+count]), count)
            return count
        directory = Dir()
        C.memset(C.byref(directory), 0xa5, C.sizeof(directory))
        before = bytes(directory)
        if path is None:
            result = lib.cp32_minix_root_open(read, len(data), C.byref(directory))
        else:
            result = lib.cp32_minix_dir_open(read, len(data), path, C.byref(directory))
        names = []
        if result:
            assert bytes(directory) == before
        else:
            while True:
                name = C.create_string_buffer(15)
                number = C.c_uint(0xdead)
                result = lib.cp32_minix_root_next(C.byref(directory), C.byref(number), name)
                if result != 1:
                    assert number.value == 0xdead
                    break
                names.append((number.value, name.value.decode('ascii')))
                assert len(names) <= 512
        assert result == expected, (result, expected, calls)
        if expected_names is not None:
            assert names == expected_names, names
        assert bytes(data) == snapshot, 'Read-only operation changed image'
        return len(calls)

    entries = [(1, '.'), (1, '..'), (2, 'boot'), (3, 'readme')]
    total = run(image, expected_names=entries)
    run(swapped_image(image), expected_names=entries)
    boot_entries = [(2, '.'), (1, '..'), (3, 'readme')]
    path_calls = run(image, path=b'/boot/', expected_names=boot_entries)
    run(swapped_image(image), path=b'boot', expected_names=boot_entries)
    for path in (b'boot/..', b'/./boot/../', b'////', b'../../'):
        run(image, path=path, expected_names=entries)
    for path in (b'readme', b'readme/..', b'boot/readme/'):
        run(image, path=path, expected=-8)
    run(image, path=b'missing', expected=-5)
    for failure in range(1, path_calls+1):
        run(image, path=b'/boot/', expected=-4, fail_at=failure)
        run(image, path=b'/boot/', expected=-4, fail_at=failure, short=True)
    shifted = bytearray(image)
    struct.pack_into('<H', shifted, 1032, 3)  # first zone, now two blocks each
    struct.pack_into('<H', shifted, 1034, 1)
    struct.pack_into('<I', shifted, 1044, 31)
    struct.pack_into('<I', shifted, 4120, 3)  # same root byte offset 6144
    run(shifted, expected_names=entries)
    for failure in range(1, total+1):
        run(image, -4, fail_at=failure)
        run(image, -4, fail_at=failure, short=True)
    for offset, fmt, value, error in (
        (4096, 'H', 0o100644, -3), (4098, 'H', 0, -3),
        (4104, 'I', 33, -3), (4104, 'I', 0, -3),
        (4104, 'I', 263*1024+16, -2), (4120, 'I', 0, -3),
        (4120, 'I', 5, -3), (4120, 'I', 63, -3),
        (4120, 'I', 0xffffffff, -3), (6144, 'H', 33, -3)):
        bad = bytearray(image)
        struct.pack_into('<'+fmt, bad, offset, value)
        run(bad, error)
    for offset, bit in ((2048, 1), (2048, 3), (3072, 1)):
        bad = bytearray(image)
        bad[offset] &= ~(1 << bit)
        run(bad, -3)
    for text, error in ((b'/', -2), (b'\x1b', -2), (b'\0', -3)):
        bad = bytearray(image)
        bad[6146:6147] = text
        run(bad, error)
    changed = bytearray(image)
    changed[6194:6208] = b'fourteen_chars'
    run(changed, expected_names=entries[:-1]+[(3, 'fourteen_chars')])
    deleted = bytearray(image)
    deleted[6176:6178] = b'\0\0'
    run(deleted, expected_names=entries[:2]+entries[3:])
    # Directory crosses the first zone; unused entries must be skipped.
    crossed = bytearray(image)
    crossed[6208:7168] = bytes(960)
    struct.pack_into('<I', crossed, 4104, 1040)
    struct.pack_into('<I', crossed, 4124, 7)
    crossed[7168:7184] = struct.pack('<H14s', 3, b'next-zone')
    run(crossed, expected_names=entries+[(3, 'next-zone')])
    struct.pack_into('<I', crossed, 4124, 6)
    run(crossed, -3)
    print('MINIX directory: fixture, endian maps/inodes, bounds, deleted entries, zone crossing and I/O failures pass')

    def file_test(data, name=b'readme', error=0, content=README, fail_at=None,
                  short=False, count=63):
        original = bytes(data)
        calls = []
        @Reader
        def read(offset, buffer, size):
            assert 0 <= offset and 0 < size <= len(data)-offset
            calls.append((offset, size))
            C.memmove(buffer, bytes(data[offset:offset+size]), size)
            if len(calls) == fail_at:
                return size-1 if short else -5
            return size
        file = File()
        C.memset(C.byref(file), 0xa5, C.sizeof(file))
        before = bytes(file)
        result = lib.cp32_minix_file_open(read, len(data), name, C.byref(file))
        output = bytearray()
        if result:
            assert bytes(file) == before
        else:
            assert lib.cp32_minix_file_read(C.byref(file), None, 0) == 0
            assert file.position == 0
            while True:
                buffer = C.create_string_buffer(b'\xa5'*65, 65)
                position = file.position
                result = lib.cp32_minix_file_read(C.byref(file), buffer, count)
                if result <= 0:
                    assert file.position == position and buffer.raw == b'\xa5'*65
                    break
                assert 0 < result <= min(count, 64)
                assert buffer.raw[result:] == b'\xa5'*(65-result)
                output.extend(buffer.raw[:result])
                assert file.position == len(output)
                assert len(output) <= 7*16384
        assert result == error, (result, error, name, calls)
        if not error:
            assert output == content, (output, content)
        assert bytes(data) == original
        return len(calls)

    file_calls = file_test(image)
    file_test(image, name=b'/readme', count=0xffffffff)
    file_test(swapped_image(image))
    scaled_file = bytearray(shifted)
    struct.pack_into('<I', scaled_file, 4248, 4)
    file_test(scaled_file)
    full_name = bytearray(image)
    full_name[6194:6208] = b'fourteen_chars'
    file_test(full_name, name=b'fourteen_chars')
    file_test(image, name=b'boot', error=-6)
    file_test(image, name=b'.', error=-6)
    for name in (b'missing', b'READ', b'README'):
        file_test(image, name=name, error=-5)
    file_test(image, name=b'/', error=-6)
    for name in (b'', b'123456789012345', b'boot/123456789012345', b'\x1b', b'/'*256):
        file_test(image, name=name, error=-7)
    for name in (b'boot/readme', b'/boot/readme', b'//boot///readme',
                 b'boot/../readme', b'./boot/./readme', b'../../readme'):
        file_test(image, name=name)
        file_test(swapped_image(image), name=name)
    file_test(image, name=b'/'*249+b'readme') # exact 255-byte boundary
    for name in (b'readme/', b'readme/.', b'readme/../readme', b'boot/readme/x'):
        file_test(image, name=name, error=-8)
    file_test(image, name=b'boot/missing', error=-5)
    for offset, fmt, value, error in (
        (4160, 'H', 0o100644, -8), (4162, 'H', 0, -3),
        (4168, 'I', 49, -3), (4184, 'I', 63, -3)):
        broken_dir = bytearray(image)
        struct.pack_into('<'+fmt, broken_dir, offset, value)
        file_test(broken_dir, name=b'boot/readme', error=error)
    nested_calls = file_test(image, name=b'/boot/readme')
    for failure in range(1, nested_calls+1):
        file_test(image, name=b'/boot/readme', error=-4, fail_at=failure)
        file_test(image, name=b'/boot/readme', error=-4, fail_at=failure, short=True)
    for failure in range(1, file_calls+1):
        file_test(image, error=-4, fail_at=failure)
        file_test(image, error=-4, fail_at=failure, short=True)
    for offset, fmt, value, error in (
        (4226, 'H', 0, -3), (4224, 'H', 0o20644, -2),
        (4232, 'I', 0xffffffff, -3), (4232, 'I', 65799*1024+1, -2),
        (4248, 'I', 5, -3), (4248, 'I', 63, -3)):
        bad = bytearray(image)
        struct.pack_into('<'+fmt, bad, offset, value)
        file_test(bad, error=error)
    empty = bytearray(image)
    struct.pack_into('<I', empty, 4232, 0)
    file_test(empty, content=b'')
    sparse = bytearray(image)
    struct.pack_into('<I', sparse, 4248, 0)
    file_test(sparse, content=bytes(len(README)))
    crossing = bytearray(image)
    content = bytes(i % 251 for i in range(1500))
    crossing[8192:8192+1500] = content
    struct.pack_into('<I', crossing, 4232, 1500)
    struct.pack_into('<I', crossing, 4252, 9)
    crossing[3072] |= 1 << 4
    file_test(crossing, content=content)
    hole = bytearray(crossing)
    struct.pack_into('<I', hole, 4252, 0)
    file_test(hole, content=content[:1024]+bytes(476))
    duplicate = bytearray(crossing)
    struct.pack_into('<I', duplicate, 4252, 8)
    file_test(duplicate, error=-3)
    crossing[3072] &= ~(1 << 4)
    file_test(crossing, error=-3)
    print('MINIX file reads: root lookup, endian, EOF, sparse zones, crossing, errors and unchanged offsets/buffers pass')

    # Single-indirect mapping: both byte orders and multi-block zones.
    for swap in (False, True):
        for shift in (0, 1):
            endian = '>' if swap else '<'
            indirect = bytearray(image)
            # Keep directory zones unscaled for lookup; open the regular inode
            # with validated geometry directly to isolate scaled read mapping.
            superblock = Super(32, 63 >> shift, 6, shift, 0x7fffffff, 1, 1, int(swap))
            zbytes = 1024 << shift
            direct, table, datazone = 10, 11, 12
            size = 8*zbytes+9
            struct.pack_into(endian+'4H4I10I', indirect, 4224,
                0o100444, 2, 0, 0, size, 0, 0, 0,
                direct, 0, 0, 0, 0, 0, 0, table, 0, 0)
            indirect[2048:2050] = struct.pack(endian+'H', 15)
            indirect[3072:3074] = struct.pack(endian+'H', 1 | sum(1 << (z-5) for z in (direct,table,datazone)))
            indirect[direct*zbytes:(direct+1)*zbytes] = b'D'*zbytes
            indirect[datazone*zbytes:(datazone+1)*zbytes] = b'I'*zbytes
            struct.pack_into(endian+'II', indirect, table*zbytes, datazone, 0)
            fail = [None]
            @Reader
            def reader(offset, buffer, count):
                assert 0 <= offset and 0 < count <= len(indirect)-offset
                C.memmove(buffer, bytes(indirect[offset:offset+count]), count)
                return count-1 if offset == fail[0] else count
            lib.cp32_fs_file_inode.argtypes = [Reader, C.POINTER(Super), C.c_uint, C.POINTER(File)]
            f = File()
            assert lib.cp32_fs_file_inode(reader, C.byref(superblock), 3, C.byref(f)) == 0
            def at(position, expected=None, error=None):
                f.position = position
                buf = C.create_string_buffer(b'Z'*64, 64)
                result = lib.cp32_minix_file_read(C.byref(f), buf, 64)
                if error is not None:
                    assert result == error and f.position == position and buf.raw == b'Z'*64
                else:
                    assert result == len(expected) and buf.raw[:result] == expected
                    assert f.position == position+result
            at(zbytes-3, b'D'*3)
            at(zbytes, bytes(64))
            at(7*zbytes-3, bytes(3))
            at(7*zbytes, b'I'*64)
            at(8*zbytes, bytes(9))
            at(size, b'')
            for offset in (table*zbytes, 3072, datazone*zbytes):
                fail[0] = offset
                at(7*zbytes, error=-4)
            fail[0] = None
            for bad in (5, superblock.zones, table, direct, 13):
                struct.pack_into(endian+'I', indirect, table*zbytes, bad)
                at(7*zbytes, error=-3)
            struct.pack_into(endian+'I', indirect, table*zbytes, 0)
            at(7*zbytes, bytes(64))
            f.indirect = 0
            at(7*zbytes, bytes(64))
            f.size = 263*zbytes
            at(262*zbytes, bytes(64))
            f.indirect = table
            struct.pack_into(endian+'I', indirect, table*zbytes+255*4, datazone)
            at(262*zbytes, b'I'*64)
            f.size += 1
            at(263*zbytes, bytes(1))
            # Invalid/unallocated/aliased indirect metadata cannot publish a handle.
            for bad in (5, superblock.zones, direct, 13):
                struct.pack_into(endian+'I', indirect, 4224+52, bad)
                C.memset(C.byref(f), 0xa5, C.sizeof(f))
                before = bytes(f)
                assert lib.cp32_fs_file_inode(reader, C.byref(superblock), 3, C.byref(f)) == -3
                assert bytes(f) == before
    print('Single-indirect reads: endian, scaled zones, holes, boundaries, corrupt pointers and short I/O pass')

    expanded = build_image(include_indirect=True)
    assert len(INDIRECT) == 7*1024+len(b'INDIRECT READ OK\n')
    assert len(expanded) == 65536 and not any(expanded[63*1024:])
    run(expanded, path=b'boot', expected_names=boot_entries+[(4, 'indirect'), (5, 'double'), (6, 'large')])
    file_test(expanded, name=b'/boot/indirect', content=INDIRECT)
    assert struct.unpack_from('<I', expanded, 4288+52)[0] == 16
    assert struct.unpack_from('<I', expanded, 16*1024)[0] == 17
    assert struct.unpack_from('<H', expanded, 4288+2)[0] == 1
    for zone in range(6, 63):
        bit = zone-5
        assert bool(expanded[3072+bit//8] & (1 << (bit%8))) == (zone <= 29)
    print('Boot indirect fixture: exact contents, inode/table ownership and scratch reservation pass')

    lib.cp32_minix_file_seek.argtypes = [C.POINTER(File), C.c_long, C.c_int]
    @Reader
    def seek_reader(offset, buffer, count):
        C.memmove(buffer, expanded[offset:offset+count], count)
        return count
    f = File()
    assert lib.cp32_minix_file_open(seek_reader, len(expanded), b'boot/indirect', C.byref(f)) == 0
    for offset, whence, expected in ((0,0,0), (7168,0,7168), (-17,2,7168),
                                    (-1,1,7167), (1,1,7168),
                                    (100,2,len(INDIRECT)+100), (0x7fffffff,0,0x7fffffff)):
        assert lib.cp32_minix_file_seek(C.byref(f), offset, whence) == 0
        assert f.position == expected
    for offset, whence in ((1,1), (-1,0), (-2147483648,0), (0,3), (0,-1),
                           (-len(INDIRECT)-1,2)):
        before = bytes(f)
        assert lib.cp32_minix_file_seek(C.byref(f), offset, whence) == -3
        assert bytes(f) == before
    assert lib.cp32_minix_file_read(C.byref(f), C.create_string_buffer(64), 64) == 0
    assert lib.cp32_minix_file_seek(C.byref(f), -17, 2) == 0
    buf = C.create_string_buffer(64)
    assert lib.cp32_minix_file_read(C.byref(f), buf, 64) == 17
    assert buf.raw[:17] == b'INDIRECT READ OK\n'
    assert lib.cp32_minix_file_seek(None,0,0) == -3
    print('Seek: start/current/end, signed limits, EOF, unchanged failures and indirect readback pass')

    f = File()
    assert lib.cp32_minix_file_open(seek_reader,len(expanded),b'boot/double',C.byref(f)) == 0
    assert lib.cp32_minix_file_seek(C.byref(f),263*1024,0) == 0
    output = b''
    while True:
        n = lib.cp32_minix_file_read(C.byref(f),buf,64)
        assert n >= 0
        if not n: break
        output += buf.raw[:n]
    assert output == DOUBLE

    for swap in (0, 1):
        for shift in (0, 1):
            order = '>' if swap else '<'
            data = bytearray(65536)
            zb = 1024 << shift
            sp = Super(32, 63 >> shift, 6, shift, 0x7fffffff, 1, 1, swap)
            struct.pack_into(order+'H',data,2048,1<<3)
            struct.pack_into(order+'H',data,3072,sum(1<<(z-5) for z in (10,11,12)))
            struct.pack_into(order+'4H4I10I',data,4224,0o100444,1,0,0,
                             65799*zb,0,0,0,*([0]*8),10,0)
            for index in (0,1,255):
                struct.pack_into(order+'I',data,10*zb+index*4,11)
                struct.pack_into(order+'I',data,11*zb+index*4,12)
            data[12*zb:13*zb] = b'X'*zb
            fail = [None]
            @Reader
            def double_reader(offset,buffer,count):
                assert 0 <= offset and 0 < count <= len(data)-offset
                C.memmove(buffer,bytes(data[offset:offset+count]),count)
                return count-1 if offset == fail[0] else count
            f = File()
            assert lib.cp32_fs_file_inode(double_reader,C.byref(sp),3,C.byref(f)) == 0
            def check(index, expected=b'X'*64, error=None):
                f.position=index*zb
                before=bytes(f)
                C.memset(buf,0x5a,64)
                n=lib.cp32_minix_file_read(C.byref(f),buf,64)
                if error is not None:
                    assert n == error and bytes(f) == before and buf.raw == b'Z'*64
                else:
                    assert n == 64 and buf.raw == expected
            for index in (263,264,518,519,65798): check(index)
            check(262,bytes(64))
            check(265,bytes(64))
            check(263+2*256,bytes(64))
            f.double_indirect=0
            check(263,bytes(64))
            f.double_indirect=10
            for offset in (10*zb,11*zb,3072,12*zb):
                fail[0]=offset
                check(263,error=-4)
            fail[0]=None
            for table, good in ((10,11),(11,12)):
                for bad in (5,sp.zones,10,11,13):
                    if bad == good: continue
                    struct.pack_into(order+'I',data,table*zb,bad)
                    check(263,error=-3)
                struct.pack_into(order+'I',data,table*zb,good)
            for bad in (5,sp.zones,13):
                struct.pack_into(order+'I',data,4224+56,bad)
                before=bytes(f)
                assert lib.cp32_fs_file_inode(double_reader,C.byref(sp),3,C.byref(f)) == -3
                assert bytes(f) == before
    print('Double indirect: endian/scaled zones, both index boundaries, holes, corrupt trees and short I/O pass')

    class Stat(C.Structure):
        _fields_ = [(n,C.c_uint) for n in ('inode','mode','links','uid','gid','size','atime','mtime','ctime')]
    lib.cp32_minix_stat.argtypes = [Reader,C.c_uint,C.c_char_p,C.POINTER(Stat)]
    for swapped in (False, True):
        data = swapped_image(image) if swapped else bytearray(image)
        order = '>' if swapped else '<'
        struct.pack_into(order+'HH',data,4224+4,1234,5678)
        struct.pack_into(order+'III',data,4224+12,100,200,300)
        calls=[]
        fail=[None]
        @Reader
        def stat_reader(offset,buffer,count):
            calls.append((offset,count))
            C.memmove(buffer,bytes(data[offset:offset+count]),count)
            return count-1 if len(calls)==fail[0] else count
        info=Stat()
        assert lib.cp32_minix_stat(stat_reader,len(data),b'boot/readme',C.byref(info))==0
        assert (info.inode,info.mode,info.links,info.uid,info.gid,info.size,info.atime,info.mtime,info.ctime)==(3,0o100444,2,1234,5678,len(README),100,200,300)
        assert (8192,len(README)) not in calls
        total=len(calls)
        for fault in range(1,total+1):
            calls.clear(); fail[0]=fault
            C.memset(C.byref(info),0xa5,C.sizeof(info)); before=bytes(info)
            assert lib.cp32_minix_stat(stat_reader,len(data),b'boot/readme',C.byref(info))==-4
            assert bytes(info)==before
        fail[0]=None
        for path,error in ((b'missing',-5),(b'readme/',-8),(b'',-7)):
            before=bytes(info)
            assert lib.cp32_minix_stat(stat_reader,len(data),path,C.byref(info))==error
            assert bytes(info)==before
        assert lib.cp32_minix_stat(stat_reader,len(data),b'/',C.byref(info))==0
        assert (info.inode,info.size,info.links)==(1,64,3)
        for offset,fmt,value,error in ((4226,'H',0,-3),(4232,'I',0xffffffff,-3),(4224,'H',0o20644,-2)):
            saved=data[offset:offset+struct.calcsize(fmt)]
            struct.pack_into(order+fmt,data,offset,value)
            before=bytes(info)
            assert lib.cp32_minix_stat(stat_reader,len(data),b'readme',C.byref(info))==error
            assert bytes(info)==before
            data[offset:offset+len(saved)]=saved
    assert lib.cp32_minix_stat(seek_reader,len(expanded),b'boot/double',C.byref(info))==0
    assert info.size==263*1024+len(DOUBLE) and info.inode==5
    print('Stat: endian metadata, timestamps/owners, directories, sparse size, errors and unchanged output pass')

    lib.cp32_minix_chdir.argtypes=[Reader,C.c_uint,C.c_void_p,C.c_char_p]
    lib.cp32_minix_abspath.argtypes=[C.c_char_p,C.c_char_p,C.c_void_p]
    for swapped in (False, True):
        data=swapped_image(image) if swapped else image
        calls=[]; fail=[None]
        @Reader
        def cwd_reader(offset,buffer,count):
            calls.append((offset,count))
            C.memmove(buffer,bytes(data[offset:offset+count]),count)
            return count-1 if len(calls)==fail[0] else count
        cwd=C.create_string_buffer(b'/',256)
        for path,expected in ((b'boot',b'/boot'),(b'.',b'/boot'),
                              (b'../boot//./',b'/boot'),(b'..',b'/'),
                              (b'../../',b'/'),(b'/boot',b'/boot')):
            assert lib.cp32_minix_chdir(cwd_reader,len(data),cwd,path)==0
            assert cwd.value==expected
        for path,error in ((b'readme',-8),(b'readme/..',-8),(b'missing',-5),
                           (b'',-7),(b'x'*256,-7)):
            before=cwd.raw
            assert lib.cp32_minix_chdir(cwd_reader,len(data),cwd,path)==error
            assert cwd.raw==before
        calls.clear()
        assert lib.cp32_minix_chdir(cwd_reader,len(data),cwd,b'/boot')==0
        total=len(calls)
        for fault in range(1,total+1):
            calls.clear(); fail[0]=fault; before=cwd.raw
            assert lib.cp32_minix_chdir(cwd_reader,len(data),cwd,b'/boot')==-4
            assert cwd.raw==before
    target=C.create_string_buffer(b'unchanged',256)
    assert lib.cp32_minix_abspath(b'/boot',b'readme/..',target)==0
    assert target.value==b'/boot/readme/..'
    assert lib.cp32_minix_abspath(b'/boot',b'/readme',target)==0
    assert target.value==b'/readme'
    assert lib.cp32_minix_abspath(b'/',b'x'*254,target)==0
    assert len(target.value)==255
    before=target.raw
    assert lib.cp32_minix_abspath(b'/',b'x'*255,target)==-7
    assert target.raw==before
    print('Cwd: relative/absolute paths, dot components, root clamp, path bounds and atomic short-I/O failures pass')

    run(expanded,path=b'boot/large',expected_names=[(6,'.'),(2,'..'),(3,'readme')])
    file_test(expanded,name=b'boot/large/readme')
    for swap in (False,True):
        data=bytearray(expanded)
        if swap:
            data=swapped_image(data)
            for ino in (4,5,6):
                off=4096+(ino-1)*64
                data[off:off+64]=struct.pack('>4H4I10I',*struct.unpack_from('<4H4I10I',expanded,off))
            for off in (7216,7232,7248,21*1024,21*1024+16,29*1024):
                data[off:off+2]=expanded[off:off+2][::-1]
            data[28*1024:28*1024+4]=struct.pack('>I',29)
        run(data,path=b'boot/large',expected_names=[(6,'.'),(2,'..'),(3,'readme')])
        for bad in (0,5,21,28,30,63):
            broken=bytearray(data)
            struct.pack_into(('>' if swap else '<')+'I',broken,28*1024,bad)
            run(broken,path=b'boot/large',expected=-3)
    print('Indirect directory: deleted slots, nested lookup, endian and invalid/hole data pointers pass')
    data=bytearray(expanded)
    fault=[None]
    @Reader
    def dir_reader(offset,buffer,count):
        C.memmove(buffer,bytes(data[offset:offset+count]),count)
        return count-1 if offset==fault[0] else count
    directory=Dir()
    assert lib.cp32_minix_dir_open(dir_reader,len(data),b'boot/large',C.byref(directory))==0
    for offset in (28*1024,3074,29*1024):
        directory.position=7*1024
        fault[0]=offset
        name=C.create_string_buffer(b'Z'*15,15); ino=C.c_uint(999)
        before=bytes(directory)
        assert lib.cp32_minix_root_next(C.byref(directory),C.byref(ino),name)==-4
        assert bytes(directory)==before and ino.value==999 and name.raw==b'Z'*15
    fault[0]=None
    for bad in (0,5,21,30,63):
        struct.pack_into('<I',data,4416+52,bad)
        before=bytes(directory)
        assert lib.cp32_minix_dir_open(dir_reader,len(data),b'boot/large',C.byref(directory))==-3
        assert bytes(directory)==before
    print('Indirect directory: failed root/metadata/data reads preserve handle and output')

    lib.cp32_fd_open.argtypes=[Reader,C.c_uint,C.c_char_p]
    lib.cp32_fd_close.argtypes=[C.c_int]
    lib.cp32_fd_read.argtypes=[C.c_int,C.c_void_p,C.c_uint]
    lib.cp32_fd_seek.argtypes=[C.c_int,C.c_long,C.c_int,C.POINTER(C.c_uint)]
    failure=[False]
    @Reader
    def fd_reader(offset,out,count):
        C.memmove(out,expanded[offset:offset+count],count)
        return count-1 if failure[0] else count
    for cycle in range(20):
        assert lib.cp32_fd_open(fd_reader,len(expanded),b'missing')==-5
        assert lib.cp32_fd_open(fd_reader,len(expanded),b'boot')==-6
        fds=[lib.cp32_fd_open(fd_reader,len(expanded),b'readme') for _ in range(8)]
        assert fds==list(range(8))
        assert lib.cp32_fd_open(fd_reader,len(expanded),b'readme')==-10
        buf=C.create_string_buffer(64)
        assert lib.cp32_fd_read(0,buf,4)==4 and buf.raw[:4]==README[:4]
        assert lib.cp32_fd_read(1,buf,4)==4 and buf.raw[:4]==README[:4]
        assert lib.cp32_fd_read(0,buf,4)==4 and buf.raw[:4]==README[4:8]
        pos=C.c_uint(999)
        assert lib.cp32_fd_seek(0,0,1,C.byref(pos))==0 and pos.value==8
        failure[0]=True; C.memset(buf,0x5a,64)
        assert lib.cp32_fd_read(0,buf,4)==-4 and buf.raw==b'Z'*64
        failure[0]=False
        assert lib.cp32_fd_seek(0,0,1,C.byref(pos))==0 and pos.value==8
        pos.value=999
        assert lib.cp32_fd_seek(0,-100,0,C.byref(pos))==-3 and pos.value==999
        assert lib.cp32_fd_close(3)==0
        assert lib.cp32_fd_close(3)==-9
        assert lib.cp32_fd_read(3,buf,1)==-9
        assert lib.cp32_fd_open(fd_reader,len(expanded),b'boot/double')==3
        assert lib.cp32_fd_seek(3,-len(DOUBLE),2,C.byref(pos))==0
        assert pos.value==263*1024
        assert lib.cp32_fd_read(3,buf,64)==64 and buf.raw==DOUBLE[:64]
        for fd in fds: assert lib.cp32_fd_close(fd)==0
    for fd in (-1,8,0x7fffffff):
        assert lib.cp32_fd_close(fd)==-9
        assert lib.cp32_fd_read(fd,None,0)==-9
        pos.value=999
        assert lib.cp32_fd_seek(fd,0,0,C.byref(pos))==-9 and pos.value==999
    print('Descriptors: independent offsets, exhaustion/reuse, failed opens/reads, seek, close and indirect data pass')

    lib.cp32_fd_dup.argtypes=[C.c_int]
    lib.cp32_fd_dup2.argtypes=[C.c_int,C.c_int]
    for cycle in range(20):
        a=lib.cp32_fd_open(fd_reader,len(expanded),b'readme')
        b=lib.cp32_fd_dup(a)
        independent=lib.cp32_fd_open(fd_reader,len(expanded),b'readme')
        assert (a,b,independent)==(0,1,2)
        assert lib.cp32_fd_read(a,buf,4)==4 and buf.raw[:4]==README[:4]
        assert lib.cp32_fd_read(b,buf,4)==4 and buf.raw[:4]==README[4:8]
        assert lib.cp32_fd_read(independent,buf,4)==4 and buf.raw[:4]==README[:4]
        assert lib.cp32_fd_seek(b,0,0,None)==0
        assert lib.cp32_fd_read(a,buf,4)==4 and buf.raw[:4]==README[:4]
        assert lib.cp32_fd_dup2(a,a)==a
        assert lib.cp32_fd_dup2(-1,independent)==-9
        assert lib.cp32_fd_read(independent,buf,4)==4 and buf.raw[:4]==README[4:8]
        assert lib.cp32_fd_dup2(a,independent)==independent
        assert lib.cp32_fd_close(a)==0
        assert lib.cp32_fd_read(b,buf,4)==4 and buf.raw[:4]==README[4:8]
        assert lib.cp32_fd_read(independent,buf,4)==4 and buf.raw[:4]==README[8:12]
        assert lib.cp32_fd_dup2(b,independent)==independent # already shared target
        # New open reuses fd zero but must not overwrite the shared description.
        a=lib.cp32_fd_open(fd_reader,len(expanded),b'boot/double')
        assert a==0
        assert lib.cp32_fd_seek(b,0,1,C.byref(pos))==0 and pos.value==12
        for fd in (a,b,independent): assert lib.cp32_fd_close(fd)==0
        a=lib.cp32_fd_open(fd_reader,len(expanded),b'readme')
        aliases=[a]+[lib.cp32_fd_dup(a) for _ in range(7)]
        assert aliases==list(range(8))
        assert lib.cp32_fd_dup(a)==-10
        assert lib.cp32_fd_dup2(a,7)==7 # full table, replacing alias succeeds
        for fd in aliases: assert lib.cp32_fd_close(fd)==0
    assert lib.cp32_fd_dup(0)==-9
    for bad in (-2,-1,8): assert lib.cp32_fd_dup2(0,bad)==-9
    print('Dup: shared offsets, independent opens, refcount lifetime, dup2 replacement/self/full-table and reuse pass')

    lib.cp32_fd_fstat.argtypes=[C.c_int,C.POINTER(Stat)]
    for swapped in (False,True):
        data=swapped_image(image) if swapped else bytearray(image)
        calls=[]; fault=[None]
        @Reader
        def metadata_reader(offset,out,count):
            calls.append((offset,count))
            C.memmove(out,bytes(data[offset:offset+count]),count)
            return count-1 if offset==fault[0] else count
        fd=lib.cp32_fd_open(metadata_reader,len(data),b'readme')
        assert fd==0
        alias=lib.cp32_fd_dup(fd)
        assert lib.cp32_fd_seek(fd,7,0,None)==0
        info=Stat(); pathinfo=Stat()
        assert lib.cp32_minix_stat(metadata_reader,len(data),b'readme',C.byref(pathinfo))==0
        calls.clear()
        assert lib.cp32_fd_fstat(alias,C.byref(info))==0 and bytes(info)==bytes(pathinfo)
        assert calls==[(2048,2),(4224,64)],calls # bitmap+inode only, no path traversal
        assert lib.cp32_fd_seek(fd,0,1,C.byref(pos))==0 and pos.value==7
        # Removing the directory entry from the synthetic image cannot change
        # which inode an already-open descriptor identifies.
        data[6192:6194]=b'\0\0'
        assert lib.cp32_minix_stat(metadata_reader,len(data),b'readme',C.byref(pathinfo))==-5
        assert lib.cp32_fd_close(fd)==0
        assert lib.cp32_fd_fstat(alias,C.byref(info))==0 and info.inode==3
        for offset in (2048,4224):
            fault[0]=offset
            before=bytes(info)
            assert lib.cp32_fd_fstat(alias,C.byref(info))==-4
            assert bytes(info)==before
            assert lib.cp32_fd_seek(alias,0,1,C.byref(pos))==0 and pos.value==7
        fault[0]=None
        assert lib.cp32_fd_fstat(alias,None)==-3
        assert lib.cp32_fd_close(alias)==0
        before=bytes(info)
        assert lib.cp32_fd_fstat(alias,C.byref(info))==-9 and bytes(info)==before
    fd=lib.cp32_fd_open(fd_reader,len(expanded),b'boot/double')
    assert lib.cp32_fd_fstat(fd,C.byref(info))==0
    assert info.inode==5 and info.size==263*1024+len(DOUBLE)
    assert lib.cp32_fd_close(fd)==0
    print('Fstat: inode identity, shared lifetime, endian fields, no path lookup/offset changes, failure atomicity and sparse size pass')
