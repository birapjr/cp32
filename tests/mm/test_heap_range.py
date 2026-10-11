"""Linker heap -> whole MM clicks, including image 120's corrupting boundary."""
from pathlib import Path
import subprocess, sys, tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
pre=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CP32_IRAM_EXT
#define PUBLIC
#define PRIVATE static
#define CLICK_SHIFT 12
#define CLICK_SIZE 4096U
#define NR_TASKS 9
#define NR_PROCS 32
#define TRUE 1
#define FALSE 0
#define OK 0
#define EINVAL -22
#define EACCES -13
typedef uintptr_t phys_bytes;
typedef uint32_t phys_clicks;
struct memory {phys_clicks base,size;} mem[3];
static phys_clicks tot_mem_size;
static uintptr_t _heap_start,_heap_end;
'''
alloc=(root/'src/mm/alloc.c').read_text().replace('#include "mm.h"','')
test=r'''
static void check(uintptr_t start,uintptr_t end,unsigned expected) {
 _heap_start=start;_heap_end=end;mem_init();
 assert(!mem[0].size && !mem[2].size && mem[1].size==expected && tot_mem_size==expected);
 if(expected) {
  assert(((uintptr_t)mem[1].base<<12)>=start);
  assert(((uintptr_t)(mem[1].base+mem[1].size)<<12)<=end);
 }
}
int main(void) {
 check(0x3fcb4c50,0x3fcd4c50,31);
 assert(mem[1].base==0x3fcb5);
 cp32_mem_allocator_ready=0;
 unsigned b=cp32_mem_alloc(8,1);assert(b==0x3fcb5);
 assert(cp32_mem_owned(b,8,1) && cp32_mem_free(b,1)==OK);
 b=cp32_mem_alloc(31,1);assert(b==0x3fcb5 && !cp32_mem_alloc(1,1));
 assert(cp32_mem_free(b,1)==OK);
 for(unsigned offset=0;offset<4096;offset++) {
  check(0x100000+offset,0x120000+offset,offset ? 31:32);
  check(0x100000+offset,0x100000+offset,0);
  check(0x100001,0x101000,0);
 }
 check(0x2000,0x1000,0);
 check(0xfffff001,0xffffffff,0);
 check(0xffffe000,0xffffffff,1);
 puts("MM heap: inward click alignment, image-120 boundary, allocation exhaustion/release and all offsets pass");
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(pre+extract_function(root/'src/kernel/misc.c','mem_init')+alloc+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
