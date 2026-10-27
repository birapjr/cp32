"""Production root device IPC validates and transfers bounded SD writes."""
from pathlib import Path
import sys,subprocess,tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define CP32_IRAM_EXT
#define PRIVATE static
#define OK 0
#define EPERM -1
#define EIO -5
#define ENXIO -6
#define EFAULT -14
#define EINVAL -22
#define EROFS -30
#define FS_PROC_NR 1
#define RAM_DEV 0
#define DEV_OPEN 1
#define DEV_CLOSE 2
#define DEV_READ 3
#define DEV_WRITE 4
#define CP32_DEV_SYNC 11
typedef struct {int m_source,DEVICE,m_type,COUNT,PROC_NR;long POSITION;char *ADDRESS;} message;
typedef uintptr_t phys_bytes;
typedef uintptr_t vir_bytes;
#define vir2phys(p) ((uintptr_t)(p))
static unsigned cap=1024,copies,reads,writes;static int fault,writable;
static unsigned cp32_root_capacity(void){return cap;}
static int cp32_root_sync(void){return fault ? -1:0;}
static int cp32_root_writable(void){return writable;}
static int cp32_root_write(unsigned o,const void *p,unsigned n){(void)p;assert(o+n<=cap && n<=512-o%512);writes++;return fault ? -1:0;}
static int cp32_root_read(unsigned o,void *p,unsigned n){assert(o+n<=cap);reads++;if(fault)return -1;memset(p,0x5a,n);return 0;}
static uintptr_t numap(int nr,uintptr_t a,unsigned n){(void)n;return nr==1 ? a:0;}
static void phys_copy(uintptr_t a,uintptr_t b,unsigned n){copies++;memcpy((void *)b,(void *)a,n);}
'''
test=r'''
int main(void){
 char p[1024];message m={1,0,DEV_WRITE,1024,1,0,p};
 assert(cp32_mem_request(&m)==EROFS && !reads && !copies);
 writable=1;assert(cp32_mem_request(&m)==1024 && writes==16);
 m.POSITION=510;m.COUNT=64;assert(cp32_mem_request(&m)==64 && writes==18);
 fault=1;assert(cp32_mem_request(&m)==EIO);fault=0;
 m.PROC_NR=0;unsigned before=writes;assert(cp32_mem_request(&m)==EFAULT && writes==before);m.PROC_NR=1;
 m.POSITION=0;m.COUNT=1024;copies=reads=0;
 m.m_type=DEV_READ;assert(cp32_mem_request(&m)==1024 && copies==16 && reads==16);
 m.POSITION=1000;m.COUNT=64;assert(cp32_mem_request(&m)==24);
 m.POSITION=1024;assert(!cp32_mem_request(&m));
 m.POSITION=0;m.ADDRESS=0;assert(cp32_mem_request(&m)==EFAULT);m.ADDRESS=p;
 fault=1;copies=0;assert(cp32_mem_request(&m)==EIO && !copies);
 m.m_type=CP32_DEV_SYNC;assert(cp32_mem_request(&m)==EIO);fault=0;assert(cp32_mem_request(&m)==OK);
 m.m_source=2;assert(cp32_mem_request(&m)==EPERM);m.m_source=1;
 cap=0;assert(cp32_mem_request(&m)==ENXIO);m.m_type=DEV_OPEN;assert(cp32_mem_request(&m)==ENXIO);
 m.m_type=DEV_READ;assert(cp32_mem_request(&m)==ENXIO);
 puts("SD IPC: writable/readonly policy, sector boundaries, no-media open/read, bounded copies, EOF and I/O errors pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+extract_function(root/'src/kernel/memory.c','cp32_mem_request')+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
