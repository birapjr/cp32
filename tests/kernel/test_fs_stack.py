"""Check the boot allocator contract for the dedicated FS stack."""
from pathlib import Path
import subprocess, sys, tempfile
sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests'))
from test_idle_handoff import extract_function
body = extract_function(root/'src/kernel/main.c', 'cp32_fs_stack_allocate')
pre = r'''
#include <assert.h>
#include <stdint.h>
#include <setjmp.h>
#define CP32_IRAM_EXT
#define CLICK_SHIFT 8
#define FS_PROC_NR 1
#define NO_NUM 0
#define CP32_FS_STACK_BYTES 8192U
typedef uint32_t reg_t;
typedef uint32_t phys_clicks;
static unsigned calls, fail;
static jmp_buf failed;
static phys_clicks cp32_mem_alloc(phys_clicks size, int owner) {
 assert(size==32 && owner==FS_PROC_NR); calls++; return fail ? 0 : 0x3fca00;
}
static void panic(const char *s,int n) { (void)s; (void)n; longjmp(failed,1); }
'''
post = r'''
int main(void) {
 assert(cp32_fs_stack_allocate()==0x3fca2000 && calls==1);
 fail=1;
 if (!setjmp(failed)) { cp32_fs_stack_allocate(); assert(0); }
 assert(calls==2);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+body+post)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('FS stack allocation: owner, size, aligned top and exhaustion panic pass')
