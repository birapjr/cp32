"""Actual exec frame and legacy SYS_EXEC/FORK handlers, modeled kernel services."""
from pathlib import Path
import sys,re,tempfile,subprocess
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
frame=re.search(r'struct stackframe_s\s*\{.*?\};',(root/'src/kernel/proc.h').read_text(),re.S).group()
pre=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define CP32_IRAM_EXT
#define PRIVATE static
#define OK 0
#define EINVAL -22
#define EACCES -13
#define E_BAD_PROC -4
#define P_SLOT_FREE 1
#define SENDING 2
#define RECEIVING 4
#define NO_MAP 8
#define PENDING 16
#define SIG_PENDING 32
#define P_STOP 64
#define SHADOWING 0
#define CHIP 4
#define INTEL 1
#define M68000 2
#define MM_PROC_NR 0
#define SIGTRAP 5
#define ANY 132
#define BYTE 255
#define FALSE 0
#define NIL_PROC ((struct proc *)0)
#define isoksusern(n) ((unsigned)(n)<4)
typedef uintptr_t reg_t;
typedef uintptr_t phys_bytes;
typedef uintptr_t vir_bytes;
typedef struct {int PROC1,PROC2,m_source,PID;void *STACK_PTR,*IP_PTR,*NAME_PTR;} message;
'''
post=r'''
struct proc {
 struct stackframe_s p_reg;
 int p_nr,p_pid,p_flags,p_alarm,p_pending,p_pendcount,p_getfrom;
 int p_blocked_frame_valid,p_blocked_frame_result;
 reg_t p_blocked_frame_pc,p_blocked_frame_psw,p_blocked_frame_sp;
 message *p_messbuf;
 long user_time,sys_time,child_utime,child_stime;
 char p_name[16];
};
static struct proc procs[4];
#define proc_addr(n) (procs+(n))
static unsigned ready,signals;
static void sigemptyset(int *p){*p=0;}
static void cause_sig(int n,int sig){assert(n==1 && sig==SIGTRAP);signals++;procs[n].p_flags|=SIG_PENDING;}
static void lock_ready(struct proc *p) {
 assert(p==procs+1 && !p->p_flags && !p->p_blocked_frame_valid && !p->p_messbuf);
 assert(p->p_reg.a[1]==p->p_reg.sp && p->p_reg.a[15]==p->p_reg.sp);
 assert(p->p_name[15]==0);ready++;
}
static phys_bytes numap(int n,vir_bytes p,vir_bytes size){assert(n==0 && size==15);return p;}
#define vir2phys(p) ((uintptr_t)(p))
static void phys_copy(phys_bytes a,phys_bytes b,phys_bytes n){memcpy((void *)b,(void *)a,n);}
'''
def handler(name):
 text=(root/'src/kernel/system.c').read_text()
 body=re.search(r'CP32_IRAM_EXT PRIVATE int '+name+r'\(m_ptr\).*?\n}\n',text,re.S).group()
 opening=body.index('{')
 return 'CP32_IRAM_EXT PRIVATE int '+name+'(message *m_ptr)\n'+body[opening:]
functions='\n'.join([extract_function(root/'src/kernel/context.c','cp32_exec_frame')]+[handler(n) for n in ('do_exec','do_fork')])
test=r'''
int main(void) {
 struct proc p,old;memset(&p,0xa5,sizeof(p));old=p;
 assert(cp32_exec_frame(&p,0,16,0)==EINVAL && !memcmp(&p,&old,sizeof(p)));
 assert(cp32_exec_frame(&p,16,17,0)==EINVAL && !memcmp(&p,&old,sizeof(p)));
 assert(!cp32_exec_frame(&p,0x403d8000,0x3fcefff0,42));
 for(unsigned i=0;i<16;i++) assert(p.p_reg.a[i]==(i==1||i==15 ? 0x3fcefff0U:i==2 ? 42U:0U));
 assert(p.p_reg.pc==0x403d8000 && p.p_reg.psw==0x100 && p.p_reg.sp==0x3fcefff0);
 assert(!p.p_reg.sar && !p.p_reg.lbeg && !p.p_reg.lend && !p.p_reg.lcount);
 assert(p.p_flags==old.p_flags);
 char name[16]="abcdefghijklmno";
 message m={1,0,0,0,(void *)0x3fcefff0,(void *)0x403d8000,name};
 memset(procs+1,0xa5,sizeof(procs[1]));procs[1].p_flags=RECEIVING;
 old=procs[1];m.m_source=1;assert(do_exec(&m)==EACCES && !memcmp(procs+1,&old,sizeof(old)));
 m.m_source=0;m.STACK_PTR=(void *)1;assert(do_exec(&m)==EINVAL && !memcmp(procs+1,&old,sizeof(old)));
 m.STACK_PTR=(void *)0x3fcefff0;assert(!do_exec(&m) && ready==1);
 assert(!strcmp(procs[1].p_name,name) && procs[1].p_getfrom==ANY);
 assert(!procs[1].p_blocked_frame_pc && !procs[1].p_blocked_frame_psw && !procs[1].p_blocked_frame_sp);
 assert(!procs[1].p_reg.a[2] && !procs[1].p_alarm);
 assert(do_exec(&m)==EINVAL && ready==1); /* runnable target */
 procs[1].p_flags=RECEIVING;m.PROC2=1;assert(!do_exec(&m) && signals==1 && ready==1);
 procs[1].p_flags=P_SLOT_FREE;assert(do_exec(&m)==E_BAD_PROC);
 procs[1].p_flags=SENDING|RECEIVING;assert(do_exec(&m)==E_BAD_PROC);
 procs[1].p_flags=RECEIVING;procs[1].p_reg.a[1]=0x3fcefff0;procs[1].p_reg.a[2]=99;
 m.PROC2=2;m.PID=77;assert(!do_fork(&m));
 assert(procs[2].p_reg.a[1]==procs[1].p_reg.a[1] && !procs[2].p_reg.a[2]);
 assert(procs[2].p_pid==77 && (procs[2].p_flags&NO_MAP));
 puts("exec context: call0 frame, stale-state clearing, publication and fork return register pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+frame+post+functions+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
