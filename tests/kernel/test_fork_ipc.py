"""Run a fork across the real SENDREC, clone, address translation and wake paths."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests'))
import test_task_ipc as ipc
from test_idle_handoff import extract_function

pre = ipc.PRELUDE.replace('NR_PROCS=4', 'NR_PROCS=5')
pre = pre.replace('  message *p_messbuf;', '''
  message *p_messbuf;
  int p_pid; unsigned p_parent_pid, p_exitstatus, p_app_pending, p_shadow;
  int p_alarm, p_pending, p_pendcount, child_utime, child_stime;
  void *p_files, *p_stguard;
  struct {unsigned mem_vir,mem_phys,mem_len;} p_map[3];
''')
# Only the physical RAM bus is modeled. Both processes pass the same Xtensa
# virtual message pointer through the real sys_call, numap and copy_message.
bus = r'''
#include "abi.h"
#define CLICK_SHIFT 12
#define NR_SEGS 3
#define NO_MAP 2
#define CP32_APP_DATA 0x3fcec000U
#define CP32_APP_TOP  0x3fcf0000U
#define istaskp(p) ((p)->p_nr<0)
enum { WINDOW = CP32_APP_DATA-16384, BACKING=0x3fcb4000, SERVER=0x3fcd1000 };
/* Addresses in the image 206 updater capture and its linked _sendrec. */
enum { TRAP_PC=0x4037447c, SENDREC_RETURN=0x403726a1, APP_FILE=2007 };
static unsigned char resident[32768], child_image[32768], server_page[4096];
static unsigned char *physical(phys_bytes addr, size_t n) {
  if(addr>=WINDOW && addr<WINDOW+sizeof(resident)){assert(n<=WINDOW+sizeof(resident)-addr);return resident+(addr-WINDOW);}
  if(addr>=BACKING && addr<BACKING+sizeof(child_image)){assert(n<=BACKING+sizeof(child_image)-addr);return child_image+(addr-BACKING);}
  if(addr>=SERVER && addr<SERVER+sizeof(server_page)){assert(n<=SERVER+sizeof(server_page)-addr);return server_page+(addr-SERVER);}
  assert(addr>UINT32_MAX);return (void *)addr;
}
static int cp32_mem_owned(unsigned base,unsigned pages,int owner) {
  return base==BACKING>>CLICK_SHIFT && pages==8 && owner==MM_PROC_NR;
}
static void phys_copy(phys_bytes src,phys_bytes dest,size_t n) {
  assert(irq_mask);memcpy(physical(dest,n),physical(src,n),n);
}
'''
memory = '\n'.join(extract_function(root/'src/kernel/system.c', name)
                   for name in ('numap', 'mem_copy'))
pre = pre[:pre.index('static phys_bytes numap(')] + bus + memory + pre[pre.index('static void cp32_trace_ipc('):]
clone = '\n'.join(extract_function(root/'src/kernel/process.c', name)
                  for name in ('process_detached', 'process_pid_free', 'cp32_proc_clone'))
setup = ipc.TESTS[:ipc.TESTS.index('static void exchange(')]
test = r'''
static void first_fork(int mm_waiting) {
  reset();
  struct proc *mm=proc_addr(MM_PROC_NR), *parent=proc_addr(3), *child=proc_addr(4);
  const unsigned v=CP32_APP_TOP-128, offset=v-WINDOW;
  message *request=(message *)(uintptr_t)v, *server=(message *)(uintptr_t)SERVER;
  message fork_request={0,APP_FILE,CP32_APP_FORK}, msg;
  parent->p_pid=101;parent->p_flags=0;parent->p_reg=mm->p_reg;
  parent->p_reg.pc=TRAP_PC;parent->p_reg.a[0]=SENDREC_RETURN;
  parent->p_reg.sp=parent->p_reg.a[1]=CP32_APP_TOP-256;
  parent->p_map[1].mem_vir=CP32_APP_DATA;
  parent->p_map[1].mem_phys=CP32_APP_DATA>>CLICK_SHIFT;parent->p_map[1].mem_len=4;
  mm->p_map[1].mem_vir=SERVER;mm->p_map[1].mem_phys=SERVER>>CLICK_SHIFT;mm->p_map[1].mem_len=1;
  memcpy(resident+offset,&fork_request,sizeof(msg));
  ready(parent);ready(mm);
  if(mm_waiting)invoke(mm,RECEIVE,ANY,server);
  invoke(parent,BOTH,MM_PROC_NR,request);
  if(!mm_waiting){
    assert(parent->p_flags==(SENDING|RECEIVING) && mm->p_callerq==parent);
    invoke(mm,RECEIVE,ANY,server);
  }
  memcpy(&msg,server_page,sizeof(msg));
  assert(msg.m_source==3 && msg.m_type==APP_FILE && msg.payload==CP32_APP_FORK);
  assert(parent->p_flags==RECEIVING && parent->p_messbuf==request && parent->p_blocked_frame_valid);

  int saved=lock_save();
  assert(cp32_proc_clone(3,4,102,MM_PROC_NR)==OK);
  assert(child->p_getfrom==MM_PROC_NR && child->p_messbuf==request && child->p_flags==(NO_MAP|RECEIVING));
  memcpy(child_image,resident,sizeof(resident));child->p_shadow=BACKING>>CLICK_SHIFT;
  child->p_flags=RECEIVING;restore_lock(saved);

  msg.m_type=0;memcpy(server_page,&msg,sizeof(msg));invoke(mm,SEND,4,server);
  memcpy(&msg,child_image+offset,sizeof(msg));assert(msg.m_source==MM_PROC_NR && msg.m_type==0);
  memcpy(&msg,resident+offset,sizeof(msg));assert(msg.m_type==APP_FILE && parent->p_flags==RECEIVING);
  msg.m_type=102;memcpy(server_page,&msg,sizeof(msg));invoke(mm,SEND,3,server);
  memcpy(&msg,resident+offset,sizeof(msg));assert(msg.m_source==MM_PROC_NR && msg.m_type==102);
  memcpy(&msg,child_image+offset,sizeof(msg));assert(msg.m_type==0);
  for(int n=3;n<=4;n++){
    struct proc *p=proc_addr(n);
    assert(!p->p_flags && !p->p_blocked_frame_valid && proc_is_ready_queued(p));
    assert(p->p_reg.pc==TRAP_PC+3 && p->p_reg.a[0]==SENDREC_RETURN && p->p_reg.a[2]==OK);
    assert(p->p_reg.sp==CP32_APP_TOP-256);
  }
}
int main(void){
  (void)cp32_boot_process_flags;
  first_fork(0);first_fork(1);
  puts("fork IPC: queued and waiting MM, inherited PC/a0, private replies and both frames runnable PASS");
}
'''
known = set(ipc.NAMES) | {'cp32_boot_process_flags'} | set(re.findall(r'\bcp32_\w+', pre))
counters = set(re.findall(r'\bcp32_\w+', ipc.BODIES)) - known
source = pre + '\n' + ''.join('static unsigned '+name+';\n' for name in sorted(counters))
source += ipc.prototypes + '\n' + ipc.BODIES + '\n' + clone + '\n' + setup + test
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.c').write_text(source)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-fno-sanitize-recover=undefined', '-iquote', str(root/'src/apps/hello'), str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
