"""Application fault policy and real exception assembly (no hardware claims)."""
from pathlib import Path
import sys,subprocess,tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
from test_irq_registers import Machine,CONSTANTS
source=extract_function(root/'src/kernel/context.c','cp32_exec_frame')
source+=extract_function(root/'src/kernel/application.c','cp32_application_fault')
source+=extract_function(root/'src/kernel/application.c','cp32_trace_app_fault')
source+=extract_function(root/'src/kernel/application.c','application_fault_take')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abi.h"
#include "io.h"
#include "app_layout.h"
#define CP32_IRAM_EXT
#define NIL_PROC ((struct proc *)0)
#define EINVAL -22
#define OK 0
#define LOW_USER 2
#define NR_PROCS 7
typedef uintptr_t reg_t;
struct proc {struct {reg_t a[16],pc,psw,sp,sar,lbeg,lend,lcount;} p_reg;int p_flags;};
static struct proc procs[NR_PROCS],*current_proc,*proc_ptr;
static volatile struct proc *cp32_irq_return_proc;
#define proc_addr(n) (procs+(n))
static int app_busy,app_endpoint,k_reenter,printed;
static struct {unsigned signal,cause,pc;} app_fault;
static void application_exit(int n){(void)n;assert(0);}
static void usbj_print(const char *s){if(!strcmp(s,"[CTX V180 fault pid="))printed++;}
static void usbj_print_u32(unsigned v){(void)v;}
static void usbj_print_hex32(unsigned v){(void)v;}
'''
test=r'''
static void setup(void){
 memset(procs,0xa5,sizeof(procs));procs[3].p_flags=0;
 app_busy=1;app_endpoint=3;k_reenter=0;app_fault.signal=0;
 current_proc=proc_ptr=procs+3;cp32_irq_return_proc=procs+1;
}
static void denied(unsigned cause,unsigned pc,unsigned ps){
 struct proc before=procs[3];unsigned sig=app_fault.signal;
 assert(!cp32_application_fault(cause,pc,ps));
 assert(!memcmp(&before,&procs[3],sizeof(before)) && app_fault.signal==sig && cp32_irq_return_proc==procs+1);
}
int main(void){
 unsigned causes[]={0,6,9,28,29},signals[]={4,8,10,11,11};
 for(unsigned n=0;n<5;n++){
  setup();assert(cp32_application_fault(causes[n],CP32_APP_TEXT+123,0x110)==procs+3);
  assert(app_fault.signal==signals[n] && app_fault.cause==causes[n] && app_fault.pc==CP32_APP_TEXT+123);
  struct proc *p=procs+3;
  assert(p->p_reg.pc==(reg_t)application_exit && p->p_reg.sp==CP32_APP_TOP-16 && p->p_reg.psw==0x100);
  assert(p->p_reg.a[1]==p->p_reg.sp && p->p_reg.a[15]==p->p_reg.sp && !p->p_reg.a[2]);
  for(int i=0;i<16;i++)if(i!=1 && i!=15)assert(!p->p_reg.a[i]);
  assert(!p->p_reg.sar && !p->p_reg.lbeg && !p->p_reg.lend && !p->p_reg.lcount);
  assert(!p->p_flags && cp32_irq_return_proc==p);
 }
 for(unsigned n=0;n<40;n++)if(n!=0 && n!=6 && n!=9 && n!=28 && n!=29){setup();denied(n,CP32_APP_TEXT,0x110);}
 setup();denied(0,CP32_APP_TEXT-1,0x110);denied(0,CP32_APP_TEXT_END,0x110);
 denied(0,CP32_APP_TEXT,0x100);denied(0,CP32_APP_TEXT,0x111);
 k_reenter=1;denied(0,CP32_APP_TEXT,0x110);k_reenter=0;
 app_busy=0;denied(0,CP32_APP_TEXT,0x110);app_busy=1;
 app_endpoint=2;denied(0,CP32_APP_TEXT,0x110);app_endpoint=NR_PROCS;denied(0,CP32_APP_TEXT,0x110);
 app_endpoint=3;current_proc=procs+4;denied(0,CP32_APP_TEXT,0x110);current_proc=procs+3;
 proc_ptr=procs+4;denied(0,CP32_APP_TEXT,0x110);proc_ptr=procs+3;
 procs[3].p_flags=2;denied(0,CP32_APP_TEXT,0x110);procs[3].p_flags=0;
 app_fault.signal=4;denied(0,CP32_APP_TEXT,0x110);
 for(int n=1;n<=65;n++){app_fault.signal=11;assert(application_fault_take(123)==11 && !app_fault.signal);}
 assert(printed==5 && !application_fault_take(123));
 assert(CP32_WIFEXITED(7<<8) && CP32_WEXITSTATUS(7<<8)==7 && !CP32_WIFSIGNALED(7<<8));
 assert(CP32_WIFSIGNALED(11) && CP32_WTERMSIG(11)==11 && !CP32_WIFEXITED(11));
 assert(!CP32_WIFSIGNALED(0x7f));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+source+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(root/'src/apps/hello'),'-I'+str(root/'src/apps/lib'),'-I'+str(root/'src/include/esp32s3'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

class Halt(Exception):pass
for entry in ['irq_kernel','irq_user']:
 for sp in [0,1,0x3fcef800,0xffffffff]:
  for cause in [0,6,9,28,29]:
   m=Machine();m.global_set('cp32_user_probe_mode',0)
   m.reg[1]=sp;m.sr['exccause']=cause;m.sr['epc1']=CONSTANTS['CP32_APP_TEXT']+32
   target=0x3fc82000;expected=[0]*16;expected[1]=expected[15]=CONSTANTS['CP32_APP_TOP']-16
   def dispatch(cpu,name):
    assert name=='cp32_application_fault'
    assert cpu.reg[2:5]==[cause,CONSTANTS['CP32_APP_TEXT']+32,0x110]
    assert cpu.reg[1]==CONSTANTS['CP32_APP_TOP']-16 and not cpu.stores
    cpu.frame_write(target,expected,0x40388000,0x100,(0,0,0,0))
    cpu.reg=[0xdeadbeef]*16;cpu.reg[2]=target
   m.dispatch=dispatch;m.run(entry)
   assert m.reg==expected and m.return_pc==0x40388000 and m.sr['ps']==0x100
   assert all(m.sr[x]==0 for x in ['sar','lbeg','lend','lcount'])
 # Kernel/outside-app PC must bypass recovery entirely. C policy rejection halts too.
 for pc,reject in [(0x40371000,False),(CONSTANTS['CP32_APP_TEXT_END'],False),(CONSTANTS['CP32_APP_TEXT'],True)]:
  m=Machine();m.global_set('cp32_user_probe_mode',0);m.sr.update(exccause=28,excvaddr=0,epc1=pc)
  def dispatch(cpu,name):
   if name=='cp32_application_fault':assert reject;cpu.reg[2]=0;return
   assert name=='exception_dump' and cpu.reg[2:6]==[28,0,pc,0x110];raise Halt
  m.dispatch=dispatch
  try:m.run(entry)
  except Halt:pass
  else:raise AssertionError('fatal exception resumed')
print('fault policy/assembly: caller gates, causes, fresh stack, complete RFE and fatal fallbacks PASS')

# Exercise the actual hello dispatcher/runtime. Only the deliberate hardware
# instruction is replaced in this host build; parent/child acceptance stays real.
app=r'''
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,forks,opens,position,waits,writes,closed,triggered;
static jmp_buf done;
void simulated_fault(int kind){assert(kind==(mode==5 ? 0:mode-1));triggered=1;longjmp(done,1);}
static void quit(int status){(void)status;assert(0);}
static int rd(char *p,unsigned n){(void)p;(void)n;assert(0);return -1;}
static int wr(const char *p,unsigned n){assert(n==18 && !memcmp(p,"Fault recovery OK\n",18));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 if(op==CP32_APP_OPEN){opens++;return opens==1 ? 3:4;}
 if(op==CP32_APP_FORK){forks++;if(mode==forks)return 0;position++;return 100+forks;}
 if(op==CP32_APP_READ){assert(fd==3 && n==1);*(char *)p='a';position++;return 1;}
 if(op==CP32_APP_WAITPID){assert(fd==100+forks && !n);*(int *)p=(forks==2 || forks==3) ? 11:4;waits++;return fd;}
 if(op==CP32_APP_SEEK_CUR){assert(fd==3 && !n);return position;}
 assert(op==CP32_APP_CLOSE && fd==3);closed++;return 0;
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=28,.read=rd,.write=wr,.exit=quit,.file=file};
 char *args[]={"hello",mode==5 ? "--crash":"--fault",0};
 if(!setjmp(done)){assert(!app_main(2,args,0,&s));assert(!mode && forks==4 && waits==4 && writes==1 && closed==1);}
 else {assert(triggered && mode);if(mode==5)assert(!forks && !opens);else assert(forks==mode && waits==mode-1 && opens==2 && !closed);}
}
'''
hello=(root/'src/apps/hello/hello.c').read_text()
trigger=extract_function(root/'src/apps/hello/hello.c','trigger_fault')
hello=hello.replace(trigger,'''extern void simulated_fault(int);
static __attribute__((noreturn)) void trigger_fault(int kind){simulated_fault(kind);__builtin_unreachable();}''')
hello=hello.replace('#include "../lib/io.h"','#include "io.h"')
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'app.c').write_text(app);(p/'hello.c').write_text(hello)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(root/'src/apps/hello'),'-I'+str(root/'src/apps/lib'),str(p/'app.c'),str(p/'hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'app')],check=True)
 for mode in range(6):subprocess.run([str(p/'app'),str(mode)],check=True)
print('fault commands: real runtime, four child branches, shared offset and foreground crash PASS')

# The fault classifier runs on the dying application's fixed4KiB stack. Its
# C frame and reset helper must stay small; the exit IPC path has a separate
# conservative budget including the128-byte trap frame.
for folder in ['build','build-updater','build-ram','build-sd-soft','build-sd-soft-updater']:
 build=root/'src'/folder
 if not (build/'cp32.elf').exists():continue
 frames={}
 for su in build.rglob('*.su'):
  for line in su.read_text().splitlines():
   fields=line.split('\t')
   if len(fields)>=2:frames[fields[0].rsplit(':',1)[-1]]=int(fields[1])
 classify=sum(frames[n] for n in ['cp32_application_fault','cp32_exec_frame','memset'])+16
 leave=sum(frames[n] for n in ['application_exit','_sendrec','cp32_task_ipc_dispatch','sys_call','mini_send','deliver_blocked_message','copy_message','mem_copy','numap'])+128
 assert classify<=512 and leave<=3072,(folder,classify,leave)
 print(f'{folder}: fault classification {classify}/512, exit IPC {leave}/3072 stack bytes PASS')
