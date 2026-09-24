#!/usr/bin/env python3
"""Exercise the production clean cache with counted and faulting block I/O."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
Reader=C.CFUNCTYPE(C.c_int,C.c_uint,C.c_void_p,C.c_int)
with tempfile.TemporaryDirectory() as tmp:
    so=Path(tmp)/'cache.so'
    subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',str(ROOT/'src/fs/cache.c'),'-o',str(so)],check=True)
    lib=C.CDLL(str(so))
    lib.cp32_cache_read.argtypes=[Reader,C.c_uint,C.c_uint,C.c_void_p,C.c_int]
    data=bytearray(i%251 for i in range(6201)); calls=[]; fail=[None]
    @Reader
    def reader(offset,out,count):
        assert offset+count<=len(data)
        calls.append((offset,count))
        C.memmove(out,bytes(data[offset:offset+count]),count)
        return count-1 if offset==fail[0] else count
    def read(offset,count=64,error=None,source=reader):
        out=C.create_string_buffer(b'Z'*64,64)
        n=lib.cp32_cache_read(source,len(data),offset,out,count)
        if error is not None:
            assert n==error and out.raw==b'Z'*64
        else:
            assert n==count and out.raw[:count]==bytes(data[offset:offset+count])
        return out.raw
    read(0); read(17); assert calls==[(0,1024)]
    read(1000); assert calls[-1]==(1024,1024) # cross-block staging
    for block in (2,3,4): read(block*1024)
    before=len(calls); read(0); assert len(calls)==before+1 # eviction
    lib.cp32_cache_invalidate(); fail[0]=1024
    read(1000,error=-4) # first block copied privately, second fill fails
    fail[0]=None; before=len(calls); read(1000)
    assert len(calls)==before+1 # failed block was not published
    lib.cp32_cache_invalidate(); read(6170,31)
    assert calls[-1]==(6144,57) # non-block-aligned capacity
    before=len(calls)
    read(6200,2,error=-3); read(0,65,error=-3); read(0,-1,error=-3)
    assert len(calls)==before
    assert lib.cp32_cache_read(reader,len(data),0,None,0)==0
    read(0); data[0]=99
    lib.cp32_cache_invalidate(); read(0) # new disk bytes visible after invalidation
    other_calls=[]
    @Reader
    def other(offset,out,count):
        other_calls.append(offset)
        return reader(offset,out,count)
    read(0,source=other); assert other_calls==[0] # reader identity changes device
    print('Cache: hits, eviction, crossing, partial capacity, I/O failure atomicity and invalidation pass')
