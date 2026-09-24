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
           for name in ('cp32_shell_disk_io', 'cp32_shell_disk_uncached', 'cp32_shell_disk_read', 'cp32_shell_disk_check', 'cp32_shell_tail_scan', 'cp32_shell_tail_start', 'cp32_shell_show', 'cp32_shell_cat', 'cp32_shell_tail', 'cp32_shell_ls', 'cp32_shell_stat', 'cp32_shell_cd', 'cp32_shell_cmp')]
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
static char console[10000];
static char cp32_shell_cwd[256]="/";
static char cp32_shell_previous[256];
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
  assert(cp32_minix_demo_init()==0); cp32_cache_invalidate();
  assert(cp32_ramdisk_checksum(0,capacity,&before)==0);
  assert(cp32_minix_super_read(cp32_shell_disk_read,capacity,&super)==0);
  assert(super.ninodes==32 && super.zones==63);
  assert(super.zones*1024 <= capacity-512);
  struct cp32_minix_dir dir;
  char name[15]; unsigned number;
  const char *names[]={".","..","boot","readme"};
  const unsigned numbers[]={1,1,2,3};
  for(int cycle=0;cycle<10;cycle++) {
    console[0]=0; cp32_shell_cat("readme");
    assert(!strcmp(console,"CP32 MINIX V2 RAM filesystem.\r\nRead-only filesystem bring-up.\r\n"));
    console[0]=0; cp32_shell_cat("/boot/readme");
    assert(!strcmp(console,"CP32 MINIX V2 RAM filesystem.\r\nRead-only filesystem bring-up.\r\n"));
    console[0]=0; cp32_shell_ls("boot");
    assert(!strcmp(console,".\r\n..\r\nreadme\r\nindirect\r\ndouble\r\nlarge\r\n"));
    assert(cp32_minix_root_open(cp32_shell_disk_read,capacity,&dir)==0);
    for(unsigned entry=0;entry<4;entry++) {
      assert(cp32_minix_root_next(&dir,&number,name)==1);
      assert(number==numbers[entry] && !strcmp(name,names[entry]));
    }
    assert(cp32_minix_root_next(&dir,&number,name)==0);
    assert(cp32_shell_disk_check()==0);
    assert(cp32_ramdisk_checksum(0,capacity,&after)==0 && before==after);
  }
  /* Full indirect fixture through production shell, MEM and backing disk. */
  console[0]=0; cp32_shell_cat("/boot/indirect");
  char expected_line[40]; unsigned cursor=0;
  for(unsigned zone=1;zone<=7;zone++) for(unsigned line=1;line<=32;line++) {
    int n=sprintf(expected_line,"Direct zone %u, line %02u",zone,line);
    while(n<31) expected_line[n++]=' ';
    expected_line[n++]='\r'; expected_line[n++]='\n';
    assert(!memcmp(console+cursor,expected_line,n)); cursor+=n;
  }
  assert(!strcmp(console+cursor,"INDIRECT READ OK\r\n"));
  assert(cp32_ramdisk_checksum(0,capacity,&after)==0 && before==after);
  console[0]=0; cp32_shell_tail("boot/indirect");
  cursor=0;
  for(unsigned line=24;line<=32;line++) {
    int n=sprintf(expected_line,"Direct zone 7, line %02u",line);
    while(n<31) expected_line[n++]=' ';
    expected_line[n++]='\r'; expected_line[n++]='\n';
    assert(!memcmp(console+cursor,expected_line,n)); cursor+=n;
  }
  assert(!strcmp(console+cursor,"INDIRECT READ OK\r\n"));
  console[0]=0; cp32_shell_tail("readme");
  assert(!strcmp(console,"CP32 MINIX V2 RAM filesystem.\r\nRead-only filesystem bring-up.\r\n"));
  console[0]=0; cp32_shell_tail("boot/double");
  cursor=0;
  for(unsigned line=2;line<=10;line++) {
    int n=sprintf(expected_line,"Double indirect line %02u\r\n",line);
    assert(!memcmp(console+cursor,expected_line,n)); cursor+=n;
  }
  assert(!strcmp(console+cursor,"DOUBLE INDIRECT READ OK\r\n"));
  assert(cp32_ramdisk_checksum(0,capacity,&after)==0 && before==after);
  console[0]=0; cp32_shell_stat("readme");
  assert(!strcmp(console,"file inode=3 size=61 links=3\r\nuid=0 gid=0 mtime=0\r\n"));
  console[0]=0; cp32_shell_stat("boot");
  assert(!strcmp(console,"directory inode=2 size=96 links=3\r\nuid=0 gid=0 mtime=0\r\n"));
  assert(cp32_minix_chdir(cp32_shell_disk_read,capacity,cp32_shell_cwd,"boot")==0);
  assert(!strcmp(cp32_shell_cwd,"/boot"));
  console[0]=0; cp32_shell_stat("double");
  assert(strstr(console,"file inode=5 size=269576 links=1")!=0);
  console[0]=0; cp32_shell_cat("readme");
  assert(strstr(console,"CP32 MINIX V2 RAM filesystem.")!=0);
  assert(cp32_minix_chdir(cp32_shell_disk_read,capacity,cp32_shell_cwd,"readme/..")==-8);
  assert(!strcmp(cp32_shell_cwd,"/boot"));
  assert(cp32_minix_chdir(cp32_shell_disk_read,capacity,cp32_shell_cwd,"..")==0);
  assert(!strcmp(cp32_shell_cwd,"/"));
  console[0]=0; cp32_shell_cd("-");
  assert(!strcmp(console,"No previous directory\r\n"));
  assert(!strcmp(cp32_shell_cwd,"/") && !cp32_shell_previous[0]);
  console[0]=0; cp32_shell_cd("boot");
  assert(!console[0] && !strcmp(cp32_shell_cwd,"/boot"));
  assert(!strcmp(cp32_shell_previous,"/"));
  console[0]=0; cp32_shell_cd("readme");
  assert(!strcmp(console,"Directory change failed: 8\r\n"));
  assert(!strcmp(cp32_shell_cwd,"/boot") && !strcmp(cp32_shell_previous,"/"));
  console[0]=0; cp32_shell_cd("-");
  assert(!strcmp(console,"/\r\n") && !strcmp(cp32_shell_previous,"/boot"));
  console[0]=0; cp32_shell_cd("-");
  assert(!strcmp(console,"/boot\r\n") && !strcmp(cp32_shell_previous,"/"));
  cp32_cache_invalidate(); requests=0; fail_request=1; console[0]=0; cp32_shell_cd("-");
  assert(!strcmp(console,"Directory change failed: 4\r\n"));
  assert(!strcmp(cp32_shell_cwd,"/boot") && !strcmp(cp32_shell_previous,"/"));
  fail_request=0;
  cp32_shell_cd("/");
  console[0]=0; cp32_shell_ls("boot/large");
  assert(!strcmp(console,".\r\n..\r\nreadme\r\n"));
  console[0]=0; cp32_shell_cat("boot/large/readme");
  assert(!strcmp(console,"CP32 MINIX V2 RAM filesystem.\r\nRead-only filesystem bring-up.\r\n"));
  cp32_cache_invalidate(); requests=0;
  console[0]=0; cp32_shell_ls("boot/large");
  assert(requests<40); /* hundreds of directory entries, tens of block reads */
  /* Raw writes, including failed attempts, must invalidate cached blocks. */
  char cache_saved[16], cache_seen[16], cache_pattern[16];
  memset(cache_pattern,'Q',sizeof(cache_pattern));
  assert(cp32_shell_disk_read(capacity-16,cache_saved,16)==16);
  assert(cp32_shell_disk_io(DEV_WRITE,capacity-16,cache_pattern,16)==16);
  assert(cp32_shell_disk_read(capacity-16,cache_seen,16)==16);
  assert(!memcmp(cache_seen,cache_pattern,16));
  requests=0; fail_request=1;
  assert(cp32_shell_disk_io(DEV_WRITE,capacity-16,cache_saved,16)==EIO);
  fail_request=0;
  assert(cp32_shell_disk_read(capacity-16,cache_seen,16)==16 && requests==2);
  assert(!memcmp(cache_seen,cache_pattern,16));
  assert(cp32_shell_disk_io(DEV_WRITE,capacity-16,cache_saved,16)==16);
  assert(cp32_shell_disk_read(capacity-16,cache_seen,16)==16);
  assert(!memcmp(cache_seen,cache_saved,16));
  for(unsigned repeat=0;repeat<12;repeat++) {
    console[0]=0; cp32_shell_cmp("readme boot/large/readme");
    assert(!strcmp(console,"Files identical\r\n"));
    console[0]=0; cp32_shell_cmp("readme boot/indirect");
    assert(!strcmp(console,"Files differ\r\n"));
    console[0]=0; cp32_shell_cmp("readme missing");
    assert(!strcmp(console,"Compare failed: 5\r\n"));
  }
  console[0]=0; cp32_shell_cmp("boot/indirect boot/indirect");
  assert(!strcmp(console,"Files identical\r\n"));
  cp32_cache_invalidate(); requests=0; fail_request=1;
  console[0]=0; cp32_shell_cmp("readme readme");
  assert(!strcmp(console,"Compare failed: 4\r\n"));
  fail_request=0;
  console[0]=0; cp32_shell_cmp("readme readme");
  assert(!strcmp(console,"Files identical\r\n"));
  console[0]=0; cp32_shell_cmp("readme");
  assert(!strcmp(console,"Usage: cmp file1 file2\r\n"));
  /* Tail line semantics, including empty and unterminated files. */
  const char *inputs[]={"", "\n", "one", "one\n",
      "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12",
      "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n"};
  const char *outputs[]={"", "\r\n", "one\r\n", "one\r\n",
      "3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n10\r\n11\r\n12\r\n",
      "3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n10\r\n11\r\n12\r\n"};
  for(unsigned test=0;test<sizeof(inputs)/sizeof(inputs[0]);test++) {
    cp32_cache_invalidate();
    unsigned n=strlen(inputs[test]);
    unsigned char size_bytes[4]={n&255,(n>>8)&255,0,0};
    assert(cp32_ramdisk_write_bytes(4232,size_bytes,4)==0);
    if(n) assert(cp32_ramdisk_write_bytes(8192,inputs[test],n)==0);
    console[0]=0; cp32_shell_tail("readme"); assert(!strcmp(console,outputs[test]));
  }
  /* Restore the boot fixture after isolated edge cases. */
  assert(cp32_minix_demo_init()==0); cp32_cache_invalidate();
  console[0]=0; cp32_shell_cat("missing"); assert(!strcmp(console,"File not found\r\n"));
  console[0]=0; cp32_shell_cat("boot"); assert(!strcmp(console,"Is a directory\r\n"));
  console[0]=0; cp32_shell_cat("readme/.."); assert(!strcmp(console,"Not a directory\r\n"));
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
                      ('cache.c','super.c','utility.c','inode.c','path.c','misc.c','filedes.c','open.c','read.c','stadir.c')],
                    str(ROOT / 'src/kernel/minix-demo.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
