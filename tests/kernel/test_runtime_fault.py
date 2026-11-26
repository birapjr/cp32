"""Current MM-owned applications use their own terminal fault record."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "tests"))
from test_idle_handoff import extract_function

source = extract_function(root / "src/kernel/context.c", "cp32_exec_frame")
source += extract_function(root / "src/kernel/application.c", "cp32_application_fault")
pre = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "abi.h"
#include "app_layout.h"
#define CP32_IRAM_EXT
#define NIL_PROC ((struct proc *)0)
#define EINVAL -22
#define OK 0
typedef uintptr_t reg_t;
struct proc {
 struct {reg_t a[16],pc,psw,sp,sar,lbeg,lend,lcount;} p_reg;
 int p_flags;
 unsigned p_exitstatus,p_fault_pc,p_fault_cause;
 int runtime_owned;
};
static struct proc procs[7],*current_proc,*proc_ptr;
static volatile struct proc *cp32_irq_return_proc;
static int k_reenter;
static int cp32_runtime_is_app(const struct proc *p){return p && p->runtime_owned;}
static void application_exit(int status){(void)status;assert(0);}
'''
tests = r'''
static void setup(unsigned n){
 memset(procs,0,sizeof(procs));current_proc=proc_ptr=procs+n;
 current_proc->runtime_owned=1;k_reenter=0;cp32_irq_return_proc=procs+1;
}
static void denied(unsigned cause,unsigned pc,unsigned ps){
 struct proc before[7];memcpy(before,procs,sizeof(procs));
 assert(!cp32_application_fault(cause,pc,ps));
 assert(!memcmp(before,procs,sizeof(procs)) && cp32_irq_return_proc==procs+1);
}
int main(void){
 unsigned causes[]={0,6,9,28,29},signals[]={4,8,10,11,11};
 for(unsigned owner=3;owner<7;owner++)for(unsigned i=0;i<5;i++){
  setup(owner);struct proc *p=current_proc;
  assert(cp32_application_fault(causes[i],CP32_APP_TEXT+16,0x110)==p);
  assert(p->p_exitstatus==signals[i] && p->p_fault_cause==causes[i] && p->p_fault_pc==CP32_APP_TEXT+16);
  assert(p->p_reg.pc==(reg_t)application_exit && p->p_reg.sp==CP32_APP_TOP-16 && p->p_reg.psw==0x100);
  assert(p->p_reg.a[1]==p->p_reg.sp && p->p_reg.a[15]==p->p_reg.sp);
  for(int j=0;j<16;j++)if(j!=1 && j!=15)assert(!p->p_reg.a[j]);
  assert(!p->p_reg.sar && !p->p_reg.lbeg && !p->p_reg.lend && !p->p_reg.lcount);
  assert(cp32_irq_return_proc==p && !p->p_flags);
 }
 setup(3);
 for(unsigned cause=0;cause<40;cause++)if(cause!=0 && cause!=6 && cause!=9 && cause!=28 && cause!=29)denied(cause,CP32_APP_TEXT,0x110);
 denied(0,CP32_APP_TEXT-1,0x110);denied(0,CP32_APP_TEXT_END,0x110);
 denied(0,CP32_APP_TEXT,0x100);denied(0,CP32_APP_TEXT,0x111);
 k_reenter=1;denied(0,CP32_APP_TEXT,0x110);k_reenter=0;
 current_proc->runtime_owned=0;denied(0,CP32_APP_TEXT,0x110);current_proc->runtime_owned=1;
 proc_ptr=procs+4;denied(0,CP32_APP_TEXT,0x110);proc_ptr=current_proc;
 current_proc->p_flags=8;denied(0,CP32_APP_TEXT,0x110);current_proc->p_flags=0;
 current_proc->p_exitstatus=4;denied(0,CP32_APP_TEXT,0x110);current_proc->p_exitstatus=0;
 current_proc=0;denied(0,CP32_APP_TEXT,0x110);
 setup(3);assert(cp32_application_fault(0,CP32_APP_TEXT+8,0x110)==procs+3);
 current_proc=proc_ptr=procs+5;current_proc->runtime_owned=1;
 assert(cp32_application_fault(28,CP32_APP_TEXT+16,0x110)==procs+5);
 assert(procs[3].p_exitstatus==4 && procs[3].p_fault_pc==CP32_APP_TEXT+8);
 assert(procs[5].p_exitstatus==11 && procs[5].p_fault_pc==CP32_APP_TEXT+16);
 puts("runtime fault: independent owners, terminal records, complete clean frames and rejection gates PASS");
}
'''
with tempfile.TemporaryDirectory(prefix="cp32-runtime-fault-") as d:
    p = Path(d)
    (p / "test.c").write_text(pre + source + tests)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                    "-I" + str(root / "src/apps/hello"),
                    "-I" + str(root / "src/include/esp32s3"),
                    str(p / "test.c"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
