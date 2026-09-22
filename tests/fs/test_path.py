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
from make_minix_demo import build_image, README

class Super(C.Structure):
    _fields_ = [(name, C.c_uint) for name in (
        'ninodes', 'zones', 'first', 'shift', 'maximum', 'imap', 'zmap', 'swap')]
Reader = C.CFUNCTYPE(C.c_int, C.c_uint, C.c_void_p, C.c_int)
class Dir(C.Structure):
    _fields_ = [('super', Super), ('read', Reader), ('size', C.c_uint),
                ('position', C.c_uint), ('zones', C.c_uint*7)]
class File(C.Structure):
    _fields_ = [('read', Reader), ('size', C.c_uint), ('position', C.c_uint),
                ('zone_bytes', C.c_uint), ('zones', C.c_uint*7)]

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
                    *[str(ROOT/'src/fs'/name) for name in ('inode.c','path.c','open.c','read.c')], '-o', path], check=True)
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

    entries = [(1, '.'), (1, '..'), (2, 'boot'), (3, 'README')]
    total = run(image, expected_names=entries)
    run(swapped_image(image), expected_names=entries)
    boot_entries = [(2, '.'), (1, '..'), (3, 'README')]
    path_calls = run(image, path=b'/boot/', expected_names=boot_entries)
    run(swapped_image(image), path=b'boot', expected_names=boot_entries)
    for path in (b'boot/..', b'/./boot/../', b'////', b'../../'):
        run(image, path=path, expected_names=entries)
    for path in (b'README', b'README/..', b'boot/README/'):
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
        (4104, 'I', 8192, -2), (4120, 'I', 0, -3),
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

    def file_test(data, name=b'README', error=0, content=README, fail_at=None,
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
    file_test(image, name=b'/README', count=0xffffffff)
    file_test(swapped_image(image))
    scaled_file = bytearray(shifted)
    struct.pack_into('<I', scaled_file, 4248, 4)
    file_test(scaled_file)
    full_name = bytearray(image)
    full_name[6194:6208] = b'fourteen_chars'
    file_test(full_name, name=b'fourteen_chars')
    file_test(image, name=b'boot', error=-6)
    file_test(image, name=b'.', error=-6)
    for name in (b'missing', b'READ', b'readme'):
        file_test(image, name=name, error=-5)
    file_test(image, name=b'/', error=-6)
    for name in (b'', b'123456789012345', b'boot/123456789012345', b'\x1b', b'/'*256):
        file_test(image, name=name, error=-7)
    for name in (b'boot/README', b'/boot/README', b'//boot///README',
                 b'boot/../README', b'./boot/./README', b'../../README'):
        file_test(image, name=name)
        file_test(swapped_image(image), name=name)
    file_test(image, name=b'/'*249+b'README') # exact 255-byte boundary
    for name in (b'README/', b'README/.', b'README/../README', b'boot/README/x'):
        file_test(image, name=name, error=-8)
    file_test(image, name=b'boot/missing', error=-5)
    for offset, fmt, value, error in (
        (4160, 'H', 0o100644, -8), (4162, 'H', 0, -3),
        (4168, 'I', 49, -3), (4184, 'I', 63, -3)):
        broken_dir = bytearray(image)
        struct.pack_into('<'+fmt, broken_dir, offset, value)
        file_test(broken_dir, name=b'boot/README', error=error)
    nested_calls = file_test(image, name=b'/boot/README')
    for failure in range(1, nested_calls+1):
        file_test(image, name=b'/boot/README', error=-4, fail_at=failure)
        file_test(image, name=b'/boot/README', error=-4, fail_at=failure, short=True)
    for failure in range(1, file_calls+1):
        file_test(image, error=-4, fail_at=failure)
        file_test(image, error=-4, fail_at=failure, short=True)
    for offset, fmt, value, error in (
        (4226, 'H', 0, -3), (4224, 'H', 0o20644, -2),
        (4232, 'I', 0xffffffff, -3), (4232, 'I', 7169, -2),
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
