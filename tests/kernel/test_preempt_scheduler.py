"""Production scheduler fairness/pin admission and MM tick notification."""
from pathlib import Path
import subprocess,sys,tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'tests'))
import test_task_ipc as ipc
from test_idle_handoff import extract_function
pre=ipc.PRELUDE.replace('NR_PROCS=4','NR_PROCS=8')
timer=extract_function(root/'src/kernel/clock.c','cp32_timer_irq_dispatch')
charge=extract_function(root/'src/kernel/clock.c','cp32_clock_charge')
# The shared host harness already owns these globals with host-width types.
timer=__import__('re').sub(r'  extern [^;]+;\n','',timer)
pre+=r'''
#define CP32_VERBOSE_DIAGNOSTICS 1
#define CP32_TIMER_TRACE_INTERVAL 1000
typedef cp32_user_frame_t cp32_irq_frame_t;
static char _stack_bottom[1],_stack_top[1];
static unsigned hardware_ticks,trace_ticks;
#define SCHED_RATE 6
static struct proc *cp32_irq_saved_owner;
static unsigned cp32_clock_accounted_ticks;
static void cp32_clock_charge(unsigned ticks);
static void cp32_trace_launch_tick(void){assert(trace_ticks==hardware_ticks);trace_ticks++;}
static int clock_handler(int irq){assert(irq==0 && trace_ticks==hardware_ticks+1);hardware_ticks++;cp32_clock_charge(1);return 1;}
static void kernel_idle_loop(void){}
static void cp32_trace_fs_return(void){}
static void cp32_systimer_report_return(void){}
static void cp32_print_irq_status(void){}
'''
setup=ipc.TESTS[:ipc.TESTS.index('static void invoke')]
test=r'''
static void runnable_apps(void){
 reset();runtime_pin=0;
 for(int n=3;n<=5;n++) {
  struct proc *p=proc_addr(n);p->p_flags=0;
  p->p_reg.pc=0x403d8100;p->p_reg.sp=0x3fceff00;p->p_reg.a[1]=p->p_reg.sp;
  ready(p);
 }
 ready(proc_addr(FS_PROC_NR));ready(proc_addr(TTY_PROC_NR));
 cp32_clock_irq_bridge_enabled=1;
}
static void rounds(unsigned *counts,int n){
 cp32_irq_frame_t frame={0};
 for(int k=0;k<n;k++) {
  cp32_irq_saved_owner=current_proc;
  k_reenter=1;cp32_timer_irq_dispatch(&frame);k_reenter=0;
  assert(current_proc==proc_ptr && !proc_ptr->p_flags);
  assert(cp32_irq_return_proc==proc_ptr);
  counts[proc_ptr->p_nr+NR_TASKS]++;
  assert(cp32_ready_queue_check());
 }
}
static void check_user_share(unsigned *counts) {
 unsigned total=0,lo=~0U,hi=0;
 for(int n=3;n<=5;n++){
  unsigned ticks=counts[n+NR_TASKS];
  if(ticks<lo)lo=ticks;
  if(ticks>hi)hi=ticks;
  total+=ticks;
 }
 assert(total==300 && hi-lo<=SCHED_RATE);
}
static void fork_return_progress(void) {
 /* A swap consumes one entire timer period. The just-installed process
  * then takes an IRQ at its fork return before executing any instruction. */
 unsigned counts[NR_TASKS+NR_PROCS]={0}, progress[2]={0}, swaps=0;
 struct proc *resident=0;
 runnable_apps();
 int blocked[]={FS_PROC_NR,TTY_PROC_NR,5};
 for(unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);i++){
  struct proc *p=proc_addr(blocked[i]);unready(p);p->p_flags=RECEIVING;
 }
 for(int i=0;i<600;i++){
  rounds(counts,1);
  struct proc *selected=current_proc;
  assert(selected->p_nr==3 || selected->p_nr==4);
  if(selected==resident)progress[selected->p_nr-3]++;
  else {swaps++;resident=selected;}
 }
 assert(swaps==600/SCHED_RATE);
 assert(progress[0]==250 && progress[1]==250);
}
int main(void){
 (void)cp32_boot_process_flags;
 unsigned counts[NR_TASKS+NR_PROCS]={0};runnable_apps();rounds(counts,900);
 check_user_share(counts);
 assert(counts[FS_PROC_NR+NR_TASKS]==300 && counts[TTY_PROC_NR+NR_TASKS]==300);
 /* One pinned application remains eligible; blocked-out siblings retain
  * their queue positions while kernel tasks and FS make progress. */
 runnable_apps();runtime_pin=proc_addr(3);memset(counts,0,sizeof(counts));rounds(counts,90);
 assert(counts[3+NR_TASKS]==30 && !counts[4+NR_TASKS] && !counts[5+NR_TASKS]);
 assert(proc_is_ready_queued(proc_addr(4)) && proc_is_ready_queued(proc_addr(5)));
 assert(counts[FS_PROC_NR+NR_TASKS]==30 && counts[TTY_PROC_NR+NR_TASKS]==30);
 runtime_pin=0;memset(counts,0,sizeof(counts));rounds(counts,900);
 check_user_share(counts);
 /* All app candidates pinned out: bounded selection still reaches servers. */
 runnable_apps();runtime_pin=proc_addr(7);memset(counts,0,sizeof(counts));rounds(counts,100);
 assert(!counts[3+NR_TASKS] && !counts[4+NR_TASKS] && !counts[5+NR_TASKS]);
 assert(counts[FS_PROC_NR+NR_TASKS]==50 && counts[TTY_PROC_NR+NR_TASKS]==50);
 /* If no service is runnable either, the published return must be IDLE,
  * never a stale runnable-but-pinned application selected on an earlier IRQ. */
 runnable_apps();unready(proc_addr(FS_PROC_NR));unready(proc_addr(TTY_PROC_NR));
 proc_addr(FS_PROC_NR)->p_flags=RECEIVING;proc_addr(TTY_PROC_NR)->p_flags=RECEIVING;
 cp32_last_selected_fs=NIL_PROC;runtime_pin=proc_addr(7);cp32_irq_return_proc=proc_addr(3);
 memset(counts,0,sizeof(counts));rounds(counts,10);assert(counts[IDLE+NR_TASKS]==10);
 assert(hardware_ticks==2000);
 fork_return_progress();
 reset();runtime_pin=0;struct proc *mm=proc_addr(MM_PROC_NR);message m={0};
 proc_ptr=current_proc=mm;assert(mini_rec(mm,ANY,&m)==OK && mm->p_flags==RECEIVING);
 interrupt(MM_PROC_NR);assert(mm->p_flags==0 && m.m_source==HARDWARE && m.m_type==HARD_INT);
 assert(proc_is_ready_queued(mm));unready(mm);
 interrupt(MM_PROC_NR);interrupt(MM_PROC_NR);assert(mm->p_int_blocked);
 memset(&m,0,sizeof(m));assert(mini_rec(mm,ANY,&m)==OK && !mm->p_int_blocked && !mm->p_flags);
 assert(m.m_source==HARDWARE && m.m_type==HARD_INT);
 assert(mini_rec(mm,ANY,&m)==OK && mm->p_flags==RECEIVING);
 /* Arbitrary user/FS targets cannot receive a synthesized device interrupt. */
 struct proc *fs=proc_addr(FS_PROC_NR);fs->p_flags=RECEIVING;fs->p_getfrom=ANY;fs->p_messbuf=&m;
 interrupt(FS_PROC_NR);assert(fs->p_flags==RECEIVING && !fs->p_int_blocked);
 /* An always-busy MM caller queue cannot starve polling/read wakeups.
  * The coalesced notification consumes one receive, preserving sender FIFO. */
 reset();runtime_pin=0;mm=proc_addr(MM_PROC_NR);fs=proc_addr(FS_PROC_NR);
 struct proc *tty=proc_addr(TTY_PROC_NR);message a={0,71,7},b={0,72,8};
 assert(mini_send(fs,MM_PROC_NR,&a)==OK && fs->p_flags==SENDING);
 assert(mini_send(tty,MM_PROC_NR,&b)==OK && tty->p_flags==SENDING);
 interrupt(MM_PROC_NR);interrupt(MM_PROC_NR);
 assert(mini_rec(mm,ANY,&m)==OK && m.m_type==HARD_INT && !mm->p_int_blocked);
 assert(mm->p_callerq==fs && fs->p_sendlink==tty && fs->p_flags==SENDING);
 assert(mini_rec(mm,ANY,&m)==OK && m.m_type==71 && m.m_source==FS_PROC_NR && !fs->p_flags);
 assert(mini_rec(mm,ANY,&m)==OK && m.m_type==72 && m.m_source==TTY_PROC_NR && !tty->p_flags);
 assert(!mm->p_callerq);
 puts("scheduler: three CPU-ready apps, fair classes, pinned admission and coalesced MM ticks PASS");
}
'''
# Shared harness has the exact extracted production scheduler and IPC bodies.
known=set(ipc.NAMES)|{'cp32_boot_process_flags','cp32_timer_irq_dispatch'}|set(__import__('re').findall(r'\bcp32_\w+',pre))
counters=set(__import__('re').findall(r'\bcp32_\w+',ipc.BODIES+timer))-known
source=pre+'\n'+''.join('static unsigned '+n+';\n' for n in sorted(counters))+ipc.prototypes+'\n'+ipc.BODIES+charge+timer+setup+test
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

# Execute the production final-return C boundary as well as assembly contract.
prepare=extract_function(root/'src/kernel/proc.c','cp32_prepare_return')
prepare_pre=r'''
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "launch_trace.h"
#define CP32_IRAM_EXT
#define PUBLIC
#define NIL_PROC ((struct proc *)0)
#define NO_NUM 0
#define OK 0
#define LOW_USER 2
struct proc {int p_flags,p_nr,metadata;struct {unsigned pc;} p_reg;};
static int stage,fail;static jmp_buf rejected;
static unsigned signal_pc,return_pc;
static const char *panic_text;
static int panic_code;
static void panic(const char *s,int n){panic_text=s;panic_code=n;longjmp(rejected,1);}
static int cp32_runtime_activate(struct proc *p){assert(p->p_nr>LOW_USER && !stage);stage=1;return !p->metadata ? -22:fail ? -16:0;}
static void cp32_runtime_signal_return(struct proc *p){
 assert(p->metadata && stage==1);
 if(signal_pc)p->p_reg.pc=signal_pc;
 stage=2;
}
void cp32_trace_launch(enum cp32_launch_step step,unsigned value){
 assert(step==LAUNCH_RETURN && stage==2);return_pc=value;stage=3;
}
'''
prepare_test=r'''
int main(void){
 struct proc app={0,3,1,{0x403d8113U}},kernel={0},invalid_app={0,4,0,{0}};
 cp32_prepare_return(&app);assert(stage==3 && return_pc==0x403d8113U);
 stage=0;signal_pc=0x4037a003U;cp32_prepare_return(&app);assert(stage==3 && return_pc==signal_pc);
 stage=0;for(int n=-9;n<=LOW_USER;n++){kernel.p_nr=n;cp32_prepare_return(&kernel);assert(!stage);}
 /* A user endpoint with broken metadata must enter activation and fail,
  * rather than being mistaken for a kernel process and returning unchecked. */
 if(!setjmp(rejected)){cp32_prepare_return(&invalid_app);assert(0);}
 assert(stage==1 && !strcmp(panic_text,"return image") && panic_code==-22);stage=0;
 fail=1;if(!setjmp(rejected)){cp32_prepare_return(&app);assert(0);}assert(stage==1 && panic_code==-16);
 stage=0;app.p_flags=8;if(!setjmp(rejected)){cp32_prepare_return(&app);assert(0);}assert(!stage);
 if(!setjmp(rejected)){cp32_prepare_return(0);assert(0);}assert(!stage);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(prepare_pre+prepare+prepare_test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-iquote',str(root/'src/kernel'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('final return: image activation, signal delivery, traced return PC and rejection paths PASS')
