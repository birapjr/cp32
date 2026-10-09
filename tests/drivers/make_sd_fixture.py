"""Host-only volume fixture when no cross-built applications are available."""
from pathlib import Path
import struct,sys
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools'))
from make_minix_demo import build_image
from make_sd_image import wrap
elf=bytearray(136);elf[:16]=b'\x7fELF\x01\x01\x01'+bytes(9)
struct.pack_into('<HHIIIIIHHHHHH',elf,16,2,94,1,0x403d8000,52,0,0,52,32,2,0,0,0)
struct.pack_into('<8I',elf,52,1,128,0x403d8000,0x403d8000,4,4,5,4)
struct.pack_into('<8I',elf,84,1,132,0x3fcec000,0x3fcec000,4,16,6,4)
elf[128:]=b'12345678'
Path(sys.argv[1]).write_bytes(wrap(build_image(True,*([elf]*6))))
