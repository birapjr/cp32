"""Corrupt real MINIX V2 fixtures; verify detection and read-only CLI exits."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from fsck_minix import Checker, InvalidImage
from make_minix_demo import build_image
from make_sd_image import wrap


def swapped(data):
    original = Checker(data)
    original.run()
    result = bytearray(data)
    def convert(offset, fmt):
        struct.pack_into('>'+fmt, result, offset, *struct.unpack_from('<'+fmt, data, offset))
    convert(1024, '6HIHHI')
    for offset in range(2048, original.inode_table, 2):
        convert(offset, 'H')
    for node in range(original.ninodes):
        convert(original.inode_table+node*64, '4H4I10I')
    for zone, owner in original.owners.items():
        offset = zone*original.zone_bytes
        if 'indirect' in owner:
            convert(offset, '256I')
        elif int(owner.split('/')[0]) in original.directories:
            for pos in range(offset, offset+original.zone_bytes, 16):
                convert(pos, 'H')
    return result


class FsckTests(unittest.TestCase):
    def assert_bad(self, data, text):
        report = Checker(data).run()
        self.assertFalse(report['clean'])
        self.assertTrue(any(text in e for e in report['errors']), report)

    def test_clean_endian_partition_sparse_and_indirect(self):
        for extended in (False, True):
            image = build_image(extended)
            for data in (image, swapped(image)):
                for volume in (data, wrap(data)):
                    self.assertTrue(Checker(volume).run()['clean'])

    def test_scaled_zones(self):
        data = bytearray(5*2048)
        struct.pack_into('<6HIHHI', data, 1024, 16,0,1,1,3,1,0x7fffffff,0x2468,0,5)
        data[2048:4096] = b'\xff'*2048
        for bit in range(2,17):
            data[2048+bit//8] &= ~(1 << (bit%8))
        data[3072] &= ~(1 << 2)
        struct.pack_into('<4H4I10I',data,4096,0o40755,2,0,0,32,0,0,0,3,*([0]*9))
        struct.pack_into('<H14sH14s',data,6144,1,b'.',1,b'..')
        for image in (data,swapped(data)):
            self.assertTrue(Checker(image).run()['clean'])

    def test_bitmap_and_reference_errors(self):
        for big in (False,True):
            data = bytearray(swapped(build_image()) if big else build_image())
            order = '>' if big else '<'
            data[3072+(1 if big else 0)] &= ~(1<<3)
            self.assert_bad(data,'referenced but bitmap marks free')
            data = bytearray(swapped(build_image()) if big else build_image())
            struct.pack_into(order+'H',data,4096+2*64+2,1)
            self.assert_bad(data,'link count')
        data=bytearray(build_image());data[3072] |= 1<<4
        self.assert_bad(data,'allocated but unreferenced')
        data=bytearray(build_image());data[2048] &= ~1
        self.assert_bad(data,'reserved bit')
        data=bytearray(build_image());data[2048] &= ~(1<<3)
        self.assert_bad(data,'references free/invalid inode')
        data=bytearray(build_image());struct.pack_into('<H',data,6144+48,65535)
        self.assert_bad(data,'references free/invalid inode')

    def test_namespace_damage(self):
        data=bytearray(build_image());struct.pack_into('<H',data,7168+16,2)
        self.assert_bad(data,'incorrect dot-dot')
        data=bytearray(build_image());data[6144+48+2:6144+48+16]=b'boot'+bytes(10)
        self.assert_bad(data,'duplicate name')
        data=bytearray(build_image());struct.pack_into('<H',data,6144+32,1)
        self.assert_bad(data,'multiple parents or cycle')
        data=bytearray(build_image());struct.pack_into('<H',data,6144+32,0)
        self.assert_bad(data,'unreachable')
        data=bytearray(build_image());struct.pack_into('<I',data,4096+8,33)
        self.assert_bad(data,'invalid size')
        data=bytearray(build_image());struct.pack_into('<I',data,4096+24,0)
        self.assert_bad(data,'sparse/missing')

    def test_dot_placement_and_bounded_errors(self):
        data=bytearray(build_image())
        data[6144:6176]=data[6160:6176]+data[6144:6160]
        self.assert_bad(data,'misplaced dot')
        checker=Checker(build_image())
        for i in range(1005):checker.error(str(i))
        self.assertEqual(checker.error_count,1005)
        self.assertEqual(len(checker.errors),1000)

    def test_mapping_damage(self):
        data=bytearray(build_image(True));struct.pack_into('<I',data,16*1024,9)
        self.assert_bad(data,'duplicate owner')
        data=bytearray(build_image(True));struct.pack_into('<I',data,18*1024,18)
        self.assert_bad(data,'duplicate owner')
        data=bytearray(build_image());struct.pack_into('<I',data,4096+2*64+24,999)
        self.assert_bad(data,'outside data area')
        data=bytearray(build_image());struct.pack_into('<I',data,4096+2*64+8,0)
        self.assert_bad(data,'beyond EOF')
        data=bytearray(build_image());struct.pack_into('<I',data,4096+2*64+60,8)
        self.assert_bad(data,'reserved tenth')

    def test_invalid_geometry(self):
        for offset,value,fmt in ((1044,99999,'I'),(1028,0,'H'),(1034,15,'H'),(1032,1,'H'),(1040,0x137f,'H')):
            data=bytearray(build_image());struct.pack_into('<'+fmt,data,offset,value)
            with self.assertRaises(InvalidImage):Checker(data)
        with self.assertRaises(InvalidImage):Checker(bytes(30))
        data=bytearray(wrap(build_image()));struct.pack_into('<I',data,458,0xffffffff)
        with self.assertRaises(InvalidImage):Checker(data)

    def test_cli_never_modifies_input(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'disk.img'
            for status,data in ((0,build_image()),(1,bytearray(build_image())),(2,b'bad')):
                if status==1:data[2048]=0
                p.write_bytes(data)
                out=subprocess.run([sys.executable,str(ROOT/'tools/fsck_minix.py'),str(p),'--json'],capture_output=True,text=True)
                self.assertEqual(out.returncode,status,out.stderr)
                self.assertEqual(json.loads(out.stdout)['clean'],status==0)
                self.assertEqual(p.read_bytes(),data)


if __name__ == '__main__':
    unittest.main()
