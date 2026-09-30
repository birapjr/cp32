#!/usr/bin/env python3
"""Boot sparse provisioning -> production MINIX descriptors -> ELF loader."""
from pathlib import Path
import subprocess, tempfile, sys
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools'))
from make_minix_demo import build_image, encode_runs
for payload in (None,b'X',bytes(range(256))*28):
    disk=build_image(True,payload)
    restored=bytearray(65536)
    for offset,data in encode_runs(disk): restored[offset:offset+len(data)]=data
    assert restored==disk
    assert disk[63*1024:]==bytes(1024)
both=build_image(True,bytes(range(256))*28,bytes(reversed(range(256)))*28)
assert both[30*1024:37*1024]==bytes(range(256))*28
assert both[37*1024:44*1024]==bytes(reversed(range(256)))*28
assert both[63*1024:]==bytes(1024)
for payload in (b'',bytes(7169)):
    try: build_image(True,payload)
    except ValueError: pass
    else: raise AssertionError('invalid payload accepted')
hello=root/'src/build/hello.elf'
echo=root/'src/build/echo.elf'
if not hello.exists() or not echo.exists():
    print('Sparse image round trips pass; real hello integration needs make hello')
    sys.exit(0)
source=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ramdisk.h"
#include "fs.h"
#include "image.h"
static int reader(unsigned off,char *p,int n) {
  return cp32_ramdisk_read_bytes(off,p,n) ? -1 : n;
}
__PRODUCTION_READER__
int main(int argc,char **argv) {
  unsigned char text[16384],data[12288], expected[7168], actual[7168];
  struct cp32_image image;
  struct cp32_minix_stat stat;
  FILE *f; unsigned size, done=0;
  assert(argc==3);
  assert(cp32_minix_demo_init()==0);
  for(int app=1;app<argc;app++) {
  done=0;f=fopen(argv[app],"rb"); assert(f);
  size=fread(expected,1,sizeof(expected),f); assert(feof(f)); fclose(f);
  int fd=cp32_fd_open(reader,65536,app==1 ? "/boot/hello" : "/boot/echo"); assert(fd>=0);
  assert(!cp32_fd_fstat(fd,&stat)); assert(stat.size==size && stat.mode==0100555);
  while(done<size) {
    unsigned n=size-done>64 ? 64 : size-done;
    assert(cp32_fd_read(fd,(char *)actual+done,n)==(int)n); done+=n;
  }
  assert(!memcmp(actual,expected,size));
  assert(!cp32_image_load(cp32_shell_app_read,&fd,size,text,data,&image));
  assert(image.entry==CP32_APP_TEXT);
  for(unsigned i=image.data.filesz;i<sizeof(data);i++) assert(data[i]==0);
  assert(!memcmp(text,expected+image.text.offset,image.text.filesz));
  assert(!memcmp(data,expected+image.data.offset,image.data.filesz));
  assert(!cp32_fd_close(fd));
  }
  memset(actual,1,1024); assert(reader(63*1024,(char *)actual,1024)==1024);
  for(unsigned i=0;i<1024;i++) assert(!actual[i]);
  puts("hello/echo: sparse provisioning, MINIX file bytes/mode, ELF loading and BSS pass");
}
'''
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
source=source.replace('__PRODUCTION_READER__',extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_app_read'))
with tempfile.TemporaryDirectory() as folder:
    p=Path(folder)
    subprocess.run([sys.executable,str(root/'tools/make_minix_demo.py'),'--image',str(p/'disk.img'),
                    '--header',str(p/'minix-demo.h'),'--hello',str(hello),'--echo',str(echo)],check=True)
    (p/'test.c').write_text(source)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',
        '-I'+str(root/'src/kernel'),'-I'+str(root/'src/fs'),'-I'+str(root/'src/mm'),'-I'+str(p),
        str(p/'test.c'),str(root/'src/kernel/minix-demo.c'),str(root/'src/kernel/ramdisk.c'),
        str(root/'src/mm/image.c'),*[str(f) for f in (root/'src/fs').glob('*.c')],'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(hello),str(echo)],check=True)
