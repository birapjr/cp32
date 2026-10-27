"""Expanded MINIX media: relocation, bitmap geometry and production FS writes."""
from pathlib import Path
import subprocess, sys, tempfile, struct
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools'))
sys.path.insert(0,str(root/'tests/fs'))
from make_minix_demo import build_image
from make_sd_image import expand, wrap
from fsck_minix import Checker
from test_fsck import swapped
source=build_image(True,hello=b'app payload'*800)
for original in (source,swapped(source)):
    expanded=expand(original,1,4096)
    old,new=Checker(original),Checker(expanded)
    old.run();new.run()
    assert not new.error_count and new.ninodes==4096 and new.nodes==old.nodes
    # Compare every owned data block by inode/logical index, including sparse
    # double-indirect payload and directory inode-number entries.
    for ino in old.nodes:
        before=struct.unpack(old.order+'4H4I10I',old.read(old.inode_table+(ino-1)*64,64))
        after=struct.unpack(new.order+'4H4I10I',new.read(new.inode_table+(ino-1)*64,64))
        assert before[:8]==after[:8]
        a,b=Checker(original),Checker(expanded)
        am=a.mapping(ino,before[4],before[8:]);bm=b.mapping(ino,after[4],after[8:])
        assert am.keys()==bm.keys()
        for index in am:assert a.read(am[index]*1024,1024)==b.read(bm[index]*1024,1024)
for size,nodes in ((0,4096),(1025,4096),(1,16),(1,65536),(1,65535)):
    try:expand(source,size,nodes)
    except ValueError:pass
    else:raise AssertionError((size,nodes))
corrupt=bytearray(source);corrupt[2048]&=~2
try:expand(corrupt)
except ValueError:pass
else:raise AssertionError('corrupt source accepted')
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fs.h"
#include "rootdisk.h"
#include <stdint.h>
static FILE *disk;
int cp32_sdcard_init(void) {return 0;}
uint64_t cp32_sdcard_sectors(void) {return 101U*1024*2;}
int cp32_sdcard_sync(void) {return 0;}
int cp32_sdcard_read(uint32_t lba,unsigned char *p) {
 if(fseek(disk,(unsigned long)lba*512,SEEK_SET))return -1;
 return fread(p,1,512,disk)==512 ? 0:-1;
}
int cp32_sdcard_write(uint32_t lba,const unsigned char *p) {
 if(fseek(disk,(unsigned long)lba*512,SEEK_SET))return -1;
 int ok=fwrite(p,1,512,disk)==512;fflush(disk);return ok ? 0:-1;
}
static int rd(unsigned at,char *p,int n) {return cp32_root_read(at,p,n) ? -1:n;}
static int wr(unsigned at,const char *p,int n) {return cp32_root_write(at,p,n) ? -1:n;}
int main(int argc,char **argv) {
 assert(argc==2);disk=fopen(argv[1],"r+b");assert(disk);
 unsigned capacity=100U*1024*1024;assert(!cp32_root_init() && cp32_root_capacity()==capacity);struct cp32_minix_super s;
 assert(!cp32_minix_super_read(rd,capacity,&s));
 assert(s.ninodes==4096 && s.zones==102399 && s.zmap_blocks>1);
 struct cp32_minix_space space;assert(!cp32_minix_space_read(rd,capacity,&space));
 char path[20];struct cp32_minix_stat st;
 for(unsigned i=0;i<80;i++) {
  snprintf(path,sizeof(path),"/n%u",i);
  assert(cp32_minix_create(rd,wr,capacity,path,"ok",2)==2);
 }
 assert(!cp32_minix_stat(rd,capacity,"/n79",&st) && st.inode>32);
 /* Occupy lower bitmap words to force a real allocator result beyond 16 bits.
  * This synthetic allocation map is used only in this disposable test copy. */
 char full[64];memset(full,255,sizeof(full));
 for(unsigned at=0;at<8192;at+=64)assert(wr((2+s.imap_blocks)*1024+at,full,64)==64);
 cp32_cache_invalidate();assert(cp32_minix_create(rd,wr,capacity,"/high","yes",3)==3);
 struct cp32_minix_file file;char out[4];
 assert(!cp32_minix_file_open(rd,capacity,"/high",&file));
 assert(file.zones[0]>65535);
 assert(cp32_minix_file_read(&file,out,3)==3 && !memcmp(out,"yes",3));
 assert(cp32_minix_patch(rd,wr,capacity,"/high",0,"new",3)==3);
 assert(!cp32_minix_file_open(rd,capacity,"/high",&file));
 assert(cp32_minix_file_read(&file,out,3)==3 && !memcmp(out,"new",3));
 assert(!cp32_minix_unlink(rd,wr,capacity,"/high"));
 fclose(disk);return 0;
}
'''
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder);image=wrap(expand(source))
    check=Checker(image);check.run();assert not check.error_count
    assert len(image)==101*1024*1024 and check.length==100*1024*1024
    (p/'sd.img').write_bytes(image);(p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',
        '-I'+str(root/'src/fs'),'-I'+str(root/'src/kernel'),str(root/'src/kernel/rootdisk.c'),str(p/'test.c'),*[str(f) for f in (root/'src/fs').glob('*.c')],
        '-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(p/'sd.img')],check=True)
print('SD image: 100MiB/4096 inodes, both byte orders, pointer relocation and production high-zone allocation pass')
