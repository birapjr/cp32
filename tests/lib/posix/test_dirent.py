#!/usr/bin/env python3
"""Production directory streams, descriptor ownership and disk fault recovery."""
import ctypes as C
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from make_minix_demo import build_image

Reader = C.CFUNCTYPE(C.c_int, C.c_uint, C.c_void_p, C.c_int)
class Dir(C.Structure):
    _fields_ = [('fd', C.c_int)]
class Entry(C.Structure):
    _fields_ = [('inode', C.c_uint), ('name', C.c_char * 15)]

with tempfile.TemporaryDirectory(prefix='cp32-dirent-') as tmp:
    binary = str(Path(tmp) / 'dirent.so')
    sources = [ROOT / 'src/fs' / name for name in
               ('super.c','utility.c','inode.c','path.c','misc.c','filedes.c','open.c','read.c','stadir.c')]
    sources += [ROOT / 'src/lib/posix' / name for name in
                ('opendir.c','readdir.c','closedir.c','rewinddir.c')]
    sources += [ROOT / 'src/lib/other' / name for name in ('seekdir.c','telldir.c')]
    subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',
                    *map(str,sources),'-o',binary],check=True)
    lib = C.CDLL(binary)
    lib.cp32_opendir.argtypes = [Reader,C.c_uint,C.c_char_p,C.POINTER(Dir)]
    lib.cp32_readdir.argtypes = [C.POINTER(Dir),C.POINTER(Entry)]
    lib.cp32_closedir.argtypes = lib.cp32_rewinddir.argtypes = [C.POINTER(Dir)]
    lib.cp32_telldir.argtypes = [C.POINTER(Dir)]
    lib.cp32_telldir.restype = C.c_long
    lib.cp32_seekdir.argtypes = [C.POINTER(Dir),C.c_long]
    lib.cp32_fd_fcntl.argtypes = [C.c_int,C.c_int,C.c_long]
    calls, fault = 0, None
    @Reader
    def read(offset,out,count):
        global calls
        calls += 1
        assert 0 <= offset and offset+count <= len(data)
        C.memmove(out,bytes(data[offset:offset+count]),count)
        return count-1 if calls == fault else count

    for endian in ('<','>'):
        original = build_image()
        data = bytearray(original)
        if endian == '>':
            for off,fmt in [(1024,'6HIHHI')]+[(4096+i*64,'4H4I10I') for i in range(3)]:
                size=struct.calcsize('<'+fmt)
                data[off:off+size]=struct.pack('>'+fmt,*struct.unpack_from('<'+fmt,original,off))
            for off in list(range(2048,4096,2))+list(range(6144,6208,16))+list(range(7168,7216,16)):
                data[off:off+2]=original[off:off+2][::-1]
        a,b = Dir(-1),Dir(-1)
        entry = Entry()
        assert lib.cp32_opendir(read,len(data),b'/',C.byref(a)) == 0
        assert lib.cp32_fd_fcntl(a.fd,1,0) == 1 # CLOEXEC
        assert lib.cp32_fd_fcntl(a.fd,3,0) == 0o4000 # NONBLOCK, read-only
        assert lib.cp32_opendir(read,len(data),b'/',C.byref(b)) == 0
        assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == 1 and entry.name == b'.'
        position = lib.cp32_telldir(C.byref(a))
        assert position == 16 and lib.cp32_telldir(C.byref(b)) == 0
        oldfd = a.fd
        assert lib.cp32_opendir(read,len(data),b'boot',C.byref(a)) == -3 and a.fd == oldfd
        assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == 1 and entry.name == b'..'
        assert lib.cp32_seekdir(C.byref(a),position) == 0
        assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == 1 and entry.name == b'..'
        for invalid in (-16,1,0x80000000,0x100000000):
            assert lib.cp32_seekdir(C.byref(a),invalid) == -3
            assert lib.cp32_telldir(C.byref(a)) == 32
        assert lib.cp32_rewinddir(C.byref(a)) == 0
        names=[]
        while lib.cp32_readdir(C.byref(a),C.byref(entry)) == 1:
            names.append(entry.name)
        assert names == [b'.',b'..',b'boot',b'readme']
        before=bytes(entry)
        assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == 0 and bytes(entry) == before
        assert lib.cp32_seekdir(C.byref(a),4096) == 0
        assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == 0
        assert lib.cp32_rewinddir(C.byref(a)) == 0
        fault=calls+1
        assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == -4
        assert bytes(entry) == before and lib.cp32_telldir(C.byref(a)) == 0
        fault=None
        assert lib.cp32_readdir(C.byref(a),None) == -3
        assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == 1 and entry.name == b'.'
        assert lib.cp32_closedir(C.byref(a)) == 0 and a.fd == -1
        # A double-close must not close a new stream reusing the descriptor.
        c=Dir(-1)
        assert lib.cp32_opendir(read,len(data),b'boot',C.byref(c)) == 0 and c.fd == oldfd
        assert lib.cp32_closedir(C.byref(a)) == -9
        assert lib.cp32_readdir(C.byref(c),C.byref(entry)) == 1
        for stream in (b,c): assert lib.cp32_closedir(C.byref(stream)) == 0
        for operation in (lib.cp32_closedir,lib.cp32_rewinddir,lib.cp32_telldir):
            assert operation(None) == -9 and operation(C.byref(a)) == -9
        assert lib.cp32_readdir(None,C.byref(entry)) == -9
        assert lib.cp32_opendir(read,len(data),b'/',None) == -3
        for path,error in ((b'readme',-8),(b'missing',-5),(b'',-7)):
            for repeat in range(12):
                assert lib.cp32_opendir(read,len(data),path,C.byref(a)) == error and a.fd == -1
        # Fail each disk read during open, including post-open fstat reads.
        calls=0
        assert lib.cp32_opendir(read,len(data),b'boot',C.byref(a)) == 0
        total=calls
        assert lib.cp32_closedir(C.byref(a)) == 0
        for fail in range(1,total+1):
            calls=0; fault=fail
            assert lib.cp32_opendir(read,len(data),b'boot',C.byref(a)) == -4 and a.fd == -1
        fault=None
        streams=[Dir(-1) for _ in range(8)]
        for i,stream in enumerate(streams):
            assert lib.cp32_opendir(read,len(data),b'/',C.byref(stream)) == 0 and stream.fd == i
        assert lib.cp32_opendir(read,len(data),b'/',C.byref(a)) == -10 and a.fd == -1
        for stream in streams: assert lib.cp32_closedir(C.byref(stream)) == 0

    data=bytearray(build_image(include_indirect=True))
    a=Dir(-1)
    assert lib.cp32_opendir(read,len(data),b'boot/large',C.byref(a)) == 0
    assert lib.cp32_seekdir(C.byref(a),32) == 0
    # Hundreds of deleted slots precede the first single-indirect entry.
    assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == 1 and entry.name == b'readme'
    position=lib.cp32_telldir(C.byref(a))
    assert position == 7*1024+16
    assert lib.cp32_seekdir(C.byref(a),position-16) == 0
    assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == 1 and entry.name == b'readme'
    assert lib.cp32_rewinddir(C.byref(a)) == 0
    assert lib.cp32_readdir(C.byref(a),C.byref(entry)) == 1 and entry.name == b'.'
    assert lib.cp32_closedir(C.byref(a)) == 0
    print('Directory streams: endian reads, flags, ownership, seek/rewind/EOF, indirect entries, limits and fault cleanup pass')
