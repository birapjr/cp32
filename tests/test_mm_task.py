#!/usr/bin/env python3
"""Execute MM's production receive loop, allocator, protocol and address map."""
from pathlib import Path
import re
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
from test_idle_handoff import extract_function
ROOT=Path(__file__).resolve().parents[1]
NAMES=['cp32_mem_allocator_init','cp32_mem_alloc','cp32_mem_free',
       'cp32_mem_owned','numap','cp32_mm_source_valid','cp32_mm_handle_request','mm_task']
bodies=[extract_function(ROOT/'src/kernel/mm.c',n) for n in NAMES]
PRELUDE=r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include <minix/cp32_mm.h>
#define CP32_IRAM_EXT
#define PRIVATE static
#define PUBLIC
#define TRUE 1
#define FALSE 0
#define NO_NUM 0
#define NR_TASKS 9
#define NR_PROCS 32
#define NR_SEGS 3
#define CLICK_SHIFT 8
#define CP32_MAX_MEM_BLOCKS 64
#define NIL_PROC ((struct proc*)0)
enum { OK=0, EINVAL=-1, EACCES=-2, ENOMEM=-3, E_BAD_FCN=-4,
       P_SLOT_FREE=1, MM_PROC_NR=0, FS_PROC_NR=1, ANY=132, D=1 };
typedef uint32_t phys_clicks;
typedef uintptr_t phys_bytes;
typedef uintptr_t vir_bytes;
typedef struct { int m_source,m_type,m1_i1; } message;
struct map { uintptr_t mem_vir,mem_phys,mem_len; };
struct proc { int p_nr,p_flags; struct map p_map[NR_SEGS]; };
static struct proc proc[NR_TASKS+NR_PROCS];
#define proc_addr(n) (&proc[(n)+NR_TASKS])
#define BEG_PROC_ADDR proc
#define END_PROC_ADDR (proc+NR_TASKS+NR_PROCS)
#define isokprocn(n) ((n)>=-NR_TASKS && (n)<NR_PROCS)
#define istaskp(p) ((p)->p_nr<0)
static struct { phys_clicks base,size; } mem[3];
struct cp32_mem_block { phys_clicks base,size; int owner,used; };
static struct cp32_mem_block cp32_mem_blocks[CP32_MAX_MEM_BLOCKS];
static int cp32_mem_allocator_ready;
static int receives,sends,traces,base;
static jmp_buf stopped;
static void usbj_print(const char *s) { (void)s; }
static void panic(const char *s,int n) { (void)s; (void)n; assert(!"unexpected panic"); }
static void cp32_trace_mm_receive(const message *m) { (void)m; traces++; }
static int receive(int source,message *m) {
  assert(source==ANY);
  if (receives==400) longjmp(stopped,1);
  memset(m,0,sizeof(*m)); m->m_source=FS_PROC_NR;
  m->m_type=(receives%2) ? CP32_MM_RELEASE : CP32_MM_ALLOCATE;
  m->m1_i1=(receives%2) ? base : 1;
  receives++; return OK;
}
static int send(int dest,message *m) {
  assert(dest==FS_PROC_NR && m->m_type==OK);
  if (sends%2==0) { base=m->m1_i1; assert(base==(int)mem[1].base); }
  sends++; return OK;
}
'''
TESTS=r'''
int main(void) {
  for (int i=0;i<NR_TASKS+NR_PROCS;i++) { proc[i].p_nr=i-NR_TASKS; proc[i].p_flags=P_SLOT_FREE; }
  proc_addr(MM_PROC_NR)->p_flags=0; proc_addr(FS_PROC_NR)->p_flags=0;
  /* Nonnegative MM cannot use the kernel-task numap shortcut. */
  assert(numap(MM_PROC_NR,0x3FCD6000,64)==0);
  struct map *map=&proc_addr(MM_PROC_NR)->p_map[D];
  map->mem_vir=0x3FC00000; map->mem_phys=0x3FC00000>>CLICK_SHIFT; map->mem_len=0x1000;
  assert(numap(MM_PROC_NR,0x3FCD6000,64)==0x3FCD6000);
  assert(numap(MM_PROC_NR,0x3FCFFFF0,32)==0);
  mem[1].base=0x3FCA00; mem[1].size=512;
  if (!setjmp(stopped)) mm_task();
  assert(receives==400 && sends==400 && traces==400);
  assert(!cp32_mem_owned(mem[1].base,1,FS_PROC_NR));
  /* Repeated service requests coalesce back into the full free region. */
  assert(cp32_mem_alloc(512,FS_PROC_NR)==mem[1].base);
  assert(cp32_mem_free(mem[1].base,FS_PROC_NR)==OK);
  message m={FS_PROC_NR,CP32_MM_ALLOCATE,2};
  assert(cp32_mm_handle_request(&m)==OK); int allocated=m.m1_i1;
  m=(message){MM_PROC_NR,CP32_MM_RELEASE,allocated};
  assert(cp32_mm_handle_request(&m)==EACCES);
  assert(cp32_mem_owned(allocated,2,FS_PROC_NR));
  m=(message){FS_PROC_NR,CP32_MM_RELEASE,allocated};
  assert(cp32_mm_handle_request(&m)==OK);
  m=(message){FS_PROC_NR,CP32_MM_RELEASE,allocated};
  assert(cp32_mm_handle_request(&m)==EINVAL);
  m=(message){FS_PROC_NR,CP32_MM_ALLOCATE,0};
  assert(cp32_mm_handle_request(&m)==ENOMEM);
  m=(message){FS_PROC_NR,CP32_MM_ALLOCATE,-1};
  assert(cp32_mm_handle_request(&m)==ENOMEM);
  m=(message){FS_PROC_NR,999,1};
  assert(cp32_mm_handle_request(&m)==E_BAD_FCN);
  m=(message){2,CP32_MM_ALLOCATE,1};
  assert(cp32_mm_handle_request(&m)==EINVAL);
  assert(cp32_mem_alloc(512,FS_PROC_NR)==mem[1].base);
  puts("MM: 200 receive/allocate/release cycles, ownership, errors, coalescing and stack mapping passed");
  return 0;
}
'''
prototypes='\n'.join(b[:b.index('{')].rstrip()+';' for b in bodies)
with tempfile.TemporaryDirectory(prefix='cp32-mm-task-') as folder:
    p=Path(folder); (p/'test.c').write_text(PRELUDE.replace('#include <minix/cp32_mm.h>', (ROOT/'src/include/minix/cp32_mm.h').read_text())+prototypes+'\n'+'\n'.join(bodies)+TESTS)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
