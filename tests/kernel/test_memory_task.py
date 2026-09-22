#!/usr/bin/env python3
"""Production MEM service and shell client with real RAM disk and modeled IPC."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from test_idle_handoff import extract_function

bodies = [extract_function(ROOT / 'src/kernel/memory.c', name)
          for name in ('cp32_mem_request', 'mem_task')]
bodies += [extract_function(ROOT / 'src/kernel/cp32-shell.c', name)
           for name in ('cp32_shell_disk_io', 'cp32_shell_disk_read', 'cp32_shell_disk_check', 'cp32_shell_cat', 'cp32_shell_ls')]
prelude = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#include <limits.h>
#include "ramdisk.h"
#include "fs.h"
#define PRIVATE static
#define PUBLIC
#define NO_NUM 0
#define vir2phys(p) ((uintptr_t)(p))
enum { OK=0, EPERM=-1, EIO=-5, ENXIO=-6, EFAULT=-14, EINVAL=-22,
       FS_PROC_NR=1, HARDWARE=-1, MEM=-4, ANY=132, RAM_DEV=0,
       DEV_READ=3, DEV_WRITE=4, DEV_IOCTL=5, DEV_OPEN=6, DEV_CLOSE=7,
       TASK_REPLY=68 };
typedef uintptr_t phys_bytes;
typedef uintptr_t vir_bytes;
/* Model the actual m2 aliases: reply fields overwrite DEVICE and PROC_NR. */
typedef struct {
  int m_source,m_type;
  struct {int i1,i2,i3;long position;char *address;} m2;
} message;
#define DEVICE m2.i1
#define PROC_NR m2.i2
#define COUNT m2.i3
#define POSITION m2.position
#define ADDRESS m2.address
#define REP_PROC_NR m2.i1
#define REP_STATUS m2.i2
static unsigned char user[1026];
static unsigned user_size=1024, copies,max_copy;
static int shell_mode, requests, fail_request, partial_write, corrupt_reply;
static int receives,sends,traces,panic_mode;
static char console[512];
static void cp32_shell_print(const char *text) {
  assert(strlen(console)+strlen(text)<sizeof(console)); strcat(console,text);
}
static void cp32_shell_print_u32(uint32_t n) {
  char text[16]; snprintf(text,sizeof(text),"%u",n); cp32_shell_print(text);
}
static jmp_buf stopped;
static phys_bytes numap(int endpoint,vir_bytes address,unsigned n) {
  if(endpoint!=FS_PROC_NR) return 0;
  if(shell_mode) return address;
  if(address<0x1000 || address-0x1000>user_size ||
     n>user_size-(address-0x1000)) return 0;
  return (uintptr_t)(user+1+address-0x1000);
}
static void phys_copy(phys_bytes a,phys_bytes b,unsigned n) {
  assert(n<=64); copies++; if(n>max_copy) max_copy=n;
  memcpy((void*)b,(void*)a,n);
}
static void panic(const char *reason,int number) {
  (void)reason; (void)number; assert(panic_mode); longjmp(stopped,2);
}
static void cp32_trace_mem_request(const message *m,int result) {
  assert(m->m_source==FS_PROC_NR && result==OK); traces++;
}
static int receive(int source,message *m) {
  assert(source==ANY);
  if(panic_mode==1) return EIO;
  if(receives==5) longjmp(stopped,1);
  memset(m,0,sizeof(*m));
  m->m_source=receives==0 ? HARDWARE : receives==1 ? 2 : FS_PROC_NR;
  m->m_type=DEV_OPEN; m->DEVICE=RAM_DEV; m->PROC_NR=17;
  receives++; return OK;
}
static int send(int dest,message *m) {
  assert(dest==FS_PROC_NR && m->m_type==TASK_REPLY);
  assert(m->REP_PROC_NR==17 && m->REP_STATUS==OK);
  sends++; return panic_mode==2 ? EIO : OK;
}
static int _sendrec(int dest,message *m);
'''
tests = r'''
static int _sendrec(int dest,message *m) {
  assert(dest==MEM); requests++;
  if(requests==fail_request && !partial_write) return EIO;
  int owner=m->PROC_NR, original_count=m->COUNT;
  m->m_source=FS_PROC_NR;
  if(requests==fail_request && partial_write) {assert(m->m_type==DEV_WRITE);m->COUNT=17;}
  int result=cp32_mem_request(m);
  m->COUNT=original_count;
  m->m_source=MEM; m->m_type=TASK_REPLY;
  m->REP_PROC_NR=owner; m->REP_STATUS=result;
  if(corrupt_reply==1) m->m_source=0;
  if(corrupt_reply==2) m->m_type=0;
  if(corrupt_reply==3) m->REP_PROC_NR=0;
  if(corrupt_reply==4) m->REP_STATUS=original_count+1;
  return OK;
}
static message request(int operation,long offset,int count) {
  message m={0}; m.m_source=FS_PROC_NR; m.m_type=operation;
  m.DEVICE=RAM_DEV;m.PROC_NR=FS_PROC_NR;m.POSITION=offset;
  m.COUNT=count;m.ADDRESS=(char*)0x1000;return m;
}
int main(void) {
  unsigned char disk[1024], saved[512], restored[512];
  cp32_ramdisk_reset(); memset(user,0xA5,sizeof(user));
  for(int i=0;i<150;i++) user[1+i]=(unsigned char)(i^0xD3);
  message m=request(DEV_WRITE,511,150);
  assert(cp32_mem_request(&m)==150 && copies==3 && max_copy==64);
  assert(cp32_ramdisk_read_bytes(511,disk,150)==0 && !memcmp(disk,user+1,150));
  memset(user+1,0x55,1024); copies=0; m.m_type=DEV_READ;
  assert(cp32_mem_request(&m)==150 && copies==3 && !memcmp(user+1,disk,150));
  assert(user[0]==0xA5 && user[151]==0x55 && user[1025]==0xA5);
  /* MINIX's usual 1 KiB block spans two backend sectors. */
  for(int i=0;i<1024;i++) user[1+i]=(unsigned char)(i*11+9);
  m=request(DEV_WRITE,1024,1024);copies=0;
  assert(cp32_mem_request(&m)==1024 && copies==16);
  memcpy(disk,user+1,1024);memset(user+1,0,1024);m.m_type=DEV_READ;
  assert(cp32_mem_request(&m)==1024 && !memcmp(user+1,disk,1024));
  assert(user[0]==0xA5 && user[1025]==0xA5);
  unsigned capacity=cp32_ramdisk_capacity();
  m=request(DEV_WRITE,capacity-3,8); memcpy(user+1,"abcdefgh",8);
  assert(cp32_mem_request(&m)==3);
  memset(user+1,0x55,1024); m.m_type=DEV_READ;
  assert(cp32_mem_request(&m)==3 && !memcmp(user+1,"abc",3) && user[4]==0x55);
  m.POSITION=capacity;copies=0;assert(cp32_mem_request(&m)==0 && !copies);
  m.POSITION=LONG_MAX;assert(cp32_mem_request(&m)==0 && !copies);
  m.POSITION=-1;assert(cp32_mem_request(&m)==EINVAL);
  m=request(DEV_READ,0,0);assert(cp32_mem_request(&m)==EINVAL);
  m.COUNT=-1;assert(cp32_mem_request(&m)==EINVAL);
  m.COUNT=INT_MAX;assert(cp32_mem_request(&m)==EFAULT);
  m=request(DEV_READ,0,1);m.ADDRESS=0;assert(cp32_mem_request(&m)==EFAULT);
  m=request(DEV_WRITE,0,1025);assert(cp32_mem_request(&m)==EFAULT);
  m=request(DEV_READ,0,8);m.PROC_NR=2;assert(cp32_mem_request(&m)==EFAULT);
  m=request(DEV_READ,0,8);m.DEVICE=1;assert(cp32_mem_request(&m)==ENXIO);
  m=request(DEV_IOCTL,0,8);assert(cp32_mem_request(&m)==EINVAL);
  m=request(DEV_WRITE,0,8);m.m_source=2;assert(cp32_mem_request(&m)==EPERM);
  m=request(DEV_OPEN,0,0);assert(cp32_mem_request(&m)==OK);
  m.m_type=DEV_CLOSE;assert(cp32_mem_request(&m)==OK);
  /* Full-buffer validation is preserved even where EOF would truncate. */
  m=request(DEV_WRITE,capacity-3,1025);assert(cp32_mem_request(&m)==EFAULT);
  assert(cp32_ramdisk_read_bytes(capacity-3,disk,3)==0 && !memcmp(disk,"abc",3));
  /* Normal task loop ignores non-FS/IRQ messages, saves reply owner before
   * alias overwrite, and stops on actual IPC failure instead of spinning. */
  assert(setjmp(stopped)!=2);
  if(receives==0) mem_task();
  assert(receives==5 && sends==3 && traces==3);
  for(panic_mode=1;panic_mode<=2;panic_mode++) {
    receives=2;
    if(setjmp(stopped)==0) {mem_task();assert(0);}
  }
  panic_mode=0;
  /* Exercise the actual shell diagnostic and driver together. */
  for(unsigned i=0;i<sizeof(saved);i++) saved[i]=(unsigned char)(i*7+3);
  assert(cp32_ramdisk_write_bytes(capacity-sizeof(saved),saved,sizeof(saved))==0);
  unsigned before,after;assert(cp32_ramdisk_checksum(0,capacity,&before)==0);
  shell_mode=1;
  /* Read-only superblock recognition goes through the real client/driver.
   * This blank RAM disk must remain byte-for-byte unchanged. */
  struct cp32_minix_super super;
  requests=0;
  assert(cp32_minix_super_read(cp32_shell_disk_read,capacity,&super)==CP32_SUPER_ABSENT);
  assert(requests==1);
  assert(cp32_ramdisk_checksum(0,capacity,&after)==0 && before==after);
  for(int cycle=0;cycle<100;cycle++) {
    requests=0;assert(cp32_shell_disk_check()==OK && requests==5);
    assert(cp32_ramdisk_checksum(0,capacity,&after)==0 && before==after);
  }
  for(int failure=1;failure<=3;failure++) {
    requests=0;fail_request=failure;assert(cp32_shell_disk_check()==EIO);
    assert(cp32_ramdisk_checksum(0,capacity,&after)==0 && before==after);
  }
  requests=0;fail_request=2;partial_write=1;
  assert(cp32_shell_disk_check()==EIO);
  assert(cp32_ramdisk_read_bytes(capacity-512,restored,512)==0 && !memcmp(saved,restored,512));
  /* A failed restoration must not be reported as success. Recover the
   * modeled disk explicitly before the next independent test case. */
  requests=0;fail_request=4;partial_write=0;assert(cp32_shell_disk_check()==EIO);
  assert(cp32_ramdisk_write_bytes(capacity-512,saved,512)==0);
  fail_request=0;
  for(corrupt_reply=1;corrupt_reply<=4;corrupt_reply++)
    assert(cp32_shell_disk_io(DEV_READ,0,(char*)restored,512)==EIO);
  /* Boot fixture, parser, directory, shell adapter, MEM and real backing
   * storage together. The scratch diagnostic must preserve the filesystem. */
  corrupt_reply=0; requests=0;
  assert(cp32_minix_demo_init()==0);
  assert(cp32_ramdisk_checksum(0,capacity,&before)==0);
  assert(cp32_minix_super_read(cp32_shell_disk_read,capacity,&super)==0);
  assert(super.ninodes==32 && super.zones==63);
  assert(super.zones*1024 <= capacity-512);
  struct cp32_minix_dir dir;
  char name[15]; unsigned number;
  const char *names[]={".","..","boot","README"};
  const unsigned numbers[]={1,1,2,3};
  for(int cycle=0;cycle<10;cycle++) {
    console[0]=0; cp32_shell_cat("README");
    assert(!strcmp(console,"CP32 MINIX V2 RAM filesystem.\r\nRead-only filesystem bring-up.\r\n"));
    console[0]=0; cp32_shell_cat("/boot/README");
    assert(!strcmp(console,"CP32 MINIX V2 RAM filesystem.\r\nRead-only filesystem bring-up.\r\n"));
    console[0]=0; cp32_shell_ls("boot");
    assert(!strcmp(console,".\r\n..\r\nREADME\r\n"));
    assert(cp32_minix_root_open(cp32_shell_disk_read,capacity,&dir)==0);
    for(unsigned entry=0;entry<4;entry++) {
      assert(cp32_minix_root_next(&dir,&number,name)==1);
      assert(number==numbers[entry] && !strcmp(name,names[entry]));
    }
    assert(cp32_minix_root_next(&dir,&number,name)==0);
    assert(cp32_shell_disk_check()==0);
    assert(cp32_ramdisk_checksum(0,capacity,&after)==0 && before==after);
  }
  console[0]=0; cp32_shell_cat("missing"); assert(!strcmp(console,"File not found\r\n"));
  console[0]=0; cp32_shell_cat("boot"); assert(!strcmp(console,"Is a directory\r\n"));
  console[0]=0; cp32_shell_cat("README/.."); assert(!strcmp(console,"Not a directory\r\n"));
  puts("MEM: mapped chunked I/O, EOF/bounds, reply aliases, IPC errors, 100 preserved-sector cycles and restoration failures passed");
}
'''
with tempfile.TemporaryDirectory(prefix='cp32-memory-task-') as folder:
    p = Path(folder)
    subprocess.run([sys.executable, str(ROOT / 'tools/make_minix_demo.py'),
                    '--image', str(p / 'demo.img'),
                    '--header', str(p / 'minix-demo.h')], check=True)
    (p / 'test.c').write_text(prelude + '\n'.join(bodies) + tests)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'src/kernel'), '-I' + str(ROOT / 'src/fs'), '-I' + str(p), str(p / 'test.c'),
                    str(ROOT / 'src/kernel/ramdisk.c'),
                    *[str(ROOT / 'src/fs' / name) for name in
                      ('super.c','utility.c','inode.c','path.c','open.c','read.c')],
                    str(ROOT / 'src/kernel/minix-demo.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
