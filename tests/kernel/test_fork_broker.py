"""Production foreground broker plus fork helpers: reply to child, restore, wait."""
from pathlib import Path
import sys,tempfile,subprocess
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
import test_fork as f
from test_idle_handoff import extract_function
pre=f.pre.replace('refs,position,fail_dup','refs,fail_dup').replace('int m_type,m_source,m1_i1;', 'int m_type,m_source,m1_i1,m1_i2,m1_i3;char *m1_p1;')
pre=pre.replace('unsigned a[16],pc,sp;', 'unsigned a[16],pc,sp,psw;')
pre=pre.replace(' void *p_stguard;', ' struct {unsigned mem_vir,mem_phys,mem_len;} p_map[3];char p_name[16];\n void *p_stguard;')
extra=r'''
static struct {unsigned signal,cause,pc;} app_fault;
static unsigned application_fault_take(int);
static int faults,yielding;
static void usbj_print(const char *s){(void)s;}
static void usbj_print_u32(unsigned n){(void)n;}
static void usbj_print_hex32(unsigned n){(void)n;}
#define ANY 132
#define D 1
#define S 2
#define CP32_APP_TEXT 0x403d8000U
#define CP32_APP_STACK 0x3fcef000U
#define APP_TEXT_DATA (CP32_APP_TEXT-0x6f0000U)
#define APP_WRITE 2001
#define APP_EXIT 2002
#define APP_GETPID 2003
#define APP_GETPPID 2004
#define APP_SBRK 2005
#define APP_READ 2006
#define APP_FILE 2007
#define CP32_APP_EXEC 10
#define CP32_APP_YIELD 45
#define CP32_APP_KILL 46
#define CP32_APP_SIGMASK 47
#define CP32_APP_SIGPENDING 48
#define CP32_APP_SIGNAL 49
#define CP32_SIGTERM 15
#define CP32_APP_FORK 40
#define CP32_APP_WAITPID 41
#define CP32_APP_TIMES 44
#define CP32_APP_GETPGRP 42
#define CP32_APP_SETSID 43
#define reg_t uintptr_t
typedef void (*cp32_app_output)(const char *,unsigned);
typedef void *cp32_image_reader;
struct cp32_image {unsigned entry;struct {unsigned memsz;} data;};
static int app_busy,app_endpoint=-1,services,received,replied;
static int application_pid(void){static int pid=99;return ++pid;}
static int cp32_image_load(void *r,void *c,uint32_t n,unsigned char *t,unsigned char *d,struct cp32_image *out){
 (void)r;(void)c;(void)n;(void)t;(void)d;memset(live,0xa5,sizeof(live));memcpy(original,live,sizeof(live));out->entry=CP32_APP_TEXT;out->data.memsz=64;return 0;
}
static int cp32_exec_stack(unsigned char *b,unsigned cap,unsigned base,unsigned argc,const char *const argv[],unsigned envc,const char *const env[],uint32_t *sp){
 (void)b;(void)cap;(void)base;(void)argc;(void)argv;(void)envc;(void)env;*sp=CP32_APP_TOP-32;return 0;
}
static void lock_ready(struct proc *p){assert(locked && !p->p_flags);bill_ptr=p;}
static int cp32_shell_app_files_start(unsigned pid){cp32_app_state.pid=pid;cp32_app_state.fds[0]=1;handles[0]=1;refs=1;return 0;}
static void cp32_shell_app_files_reset(void);
static int cp32_shell_app_files_owner(unsigned pid){return cp32_app_state.pid==pid;}
static int cp32_shell_app_input(char *p,unsigned n){(void)p;(void)n;assert(0);return -1;}
struct cp32_app_times {unsigned user,system,children_user,children_system,uptime;};
static int application_times(struct proc *,message *);
static unsigned get_uptime(void){assert(locked);return 1234;}
static int application_replace(struct proc *p,message *m,uint32_t *fl,uint32_t *br){(void)p;(void)m;(void)fl;(void)br;return -8;}
static int application_file_request(message *m){(void)m;return -22;}
static int application_break(uint32_t f,uint32_t *b,int n){(void)f;(void)b;(void)n;return -1;}
static void output(const char *p,unsigned n){(void)p;(void)n;assert(0);}
static int receive(int endpoint,message *m){
 memset(m,0,sizeof(*m));struct proc *p=proc_addr(endpoint);
 p->p_flags=RECEIVING;p->p_getfrom=FS_PROC_NR;p->p_messbuf=m;
 if(yielding==13){
  int step=received++;m->m_type=APP_FILE;
  assert(endpoint==((step==6 || step==7 || step==11) ? 4:3));
  switch(step){
   case 0:m->m1_i1=CP32_APP_SIGMASK;m->m1_i3=1<<14;break;
   case 1:case 6:m->m1_i1=CP32_APP_KILL;m->m1_i2=step==1 ? 100:101;m->m1_i3=15;break;
   case 2:case 12:m->m1_i1=CP32_APP_SIGNAL;m->m1_i2=15;m->m1_i3=step==2 ? 1:0;break;
   case 3:m->m1_i1=CP32_APP_SIGPENDING;break;
   case 4:m->m1_i1=CP32_APP_SIGMASK;m->m1_i2=2;break;
   case 5:m->m1_i1=CP32_APP_FORK;break;
   case 7:m->m1_i1=CP32_APP_YIELD;break;
   case 8:m->m1_i1=CP32_APP_KILL;m->m1_i2=101;m->m1_i3=15;break;
   case 9:case 10:m->m1_i1=CP32_APP_WAITPID;m->m1_i2=101;m->m1_i3=step==9 ? 1:0;break;
   default:assert(step==11 || step==13);m->m_type=APP_EXIT;
  }return 0;
 }
 if(yielding>=10){
  int step=received++;m->m_type=APP_FILE;
  if(yielding==12 || yielding==14) {
   int sig=yielding==14 ? 10:15;assert(endpoint==3);
   switch(step){
    case 0:m->m1_i1=CP32_APP_SIGMASK;m->m1_i3=1<<(sig-1);break;
    case 1:m->m1_i1=CP32_APP_KILL;m->m1_i2=100;m->m1_i3=sig;break;
    case 2:m->m1_i1=CP32_APP_SIGPENDING;break;
    case 3:m->m1_i1=CP32_APP_SIGMASK;m->m1_i2=2;m->m1_i3=1<<15;break;
    case 4:m->m1_i1=CP32_APP_SIGMASK;m->m1_i2=4;break;
    default:assert(step==5);m->m1_i1=CP32_APP_SIGMASK;m->m1_i2=2;
   }
  } else {
   assert(endpoint==((step==2 || step==7 || step==8) ? 4:3));
   switch(step){
    case 0:m->m1_i1=CP32_APP_SIGMASK;m->m1_i3=1<<14;break;
    case 1:m->m1_i1=CP32_APP_FORK;break;
    case 2:case 6:m->m1_i1=CP32_APP_YIELD;break;
    case 3:case 4:m->m1_i1=CP32_APP_KILL;m->m1_i2=101;m->m1_i3=15;break;
    case 5:m->m1_i1=CP32_APP_WAITPID;m->m1_i2=101;m->m1_i3=1;break;
    case 7:m->m1_i1=CP32_APP_SIGPENDING;break;
    case 8:m->m1_i1=CP32_APP_SIGMASK;m->m1_i2=yielding==10 ? 1:2;m->m1_i3=yielding==10 ? 1<<14:0;break;
    case 9:m->m1_i1=CP32_APP_WAITPID;m->m1_i2=101;m->m1_p1=(char *)CP32_APP_DATA;break;
    case 10:m->m1_i1=CP32_APP_SIGMASK;m->m1_i2=4;break;
    default:assert(step==11);m->m_type=APP_EXIT;
   }
  }return 0;
 }
 if(yielding>=6){
  int step=received++;m->m_type=APP_FILE;
  if(yielding==9) {
   assert(endpoint==3);m->m1_i1=CP32_APP_KILL;m->m1_i2=100;
   switch(step){
    case 0:break;
    case 1:m->m1_i2=0;m->m1_i3=9;break;
    case 2:m->m1_i3=16;break;
    case 3:m->m1_p1=(char *)1;break;
    case 4:m->m1_i2=9999;break;
    case 5:m->m1_i2=1;m->m1_i3=9;break;
    default:assert(step==6);m->m1_i3=9;
   }
  } else if(yielding==6) {
   assert(endpoint==(step==1 ? 4:3));
   switch(step) {
    case 0:m->m1_i1=CP32_APP_FORK;break;
    case 1:memset(live,0x22,sizeof(live));m->m1_i1=CP32_APP_YIELD;break;
    case 2:case 4:case 6:m->m1_i1=CP32_APP_KILL;m->m1_i2=101;break;
    case 3:m->m1_i1=CP32_APP_KILL;m->m1_i2=101;m->m1_i3=9;break;
    case 5:m->m1_i1=CP32_APP_WAITPID;m->m1_i2=101;m->m1_p1=(char *)CP32_APP_DATA;break;
    default:assert(step==7);m->m_type=APP_EXIT;
   }
  } else if(yielding==7) {
   assert(endpoint==(step==1 ? 4:3));
   switch(step) {
    case 0:m->m1_i1=CP32_APP_FORK;break;
    case 1:m->m1_i1=CP32_APP_KILL;m->m1_i2=101;m->m1_i3=15;break;
    case 2:m->m1_i1=CP32_APP_WAITPID;m->m1_i2=101;m->m1_p1=(char *)CP32_APP_DATA;break;
    default:assert(step==3);m->m_type=APP_EXIT;
   }
  } else {
   static const int owners[]={3,4,5,4,4,4,3,3};assert(step<8 && endpoint==owners[step]);
   switch(step){
    case 0:case 1:m->m1_i1=CP32_APP_FORK;break;
    case 2:m->m1_i1=CP32_APP_YIELD;break;
    case 3:m->m1_i1=CP32_APP_KILL;m->m1_i2=102;m->m1_i3=15;break;
    case 4:m->m1_i1=CP32_APP_WAITPID;m->m1_i2=102;m->m1_p1=(char *)CP32_APP_DATA;break;
    case 6:m->m1_i1=CP32_APP_WAITPID;m->m1_i2=101;break;
    default:m->m_type=APP_EXIT;
   }
  }return 0;
 }
 if(yielding>=4){
  m->m_type=APP_FILE;
  switch(received++) {
   case 0:assert(endpoint==3);m->m1_i1=CP32_APP_FORK;break;
   case 1:assert(endpoint==4);memset(live,0x22,sizeof(live));m->m1_i1=CP32_APP_FORK;break;
   case 2:assert(endpoint==5);memset(live,0x33,sizeof(live));m->m1_i1=CP32_APP_YIELD;break;
   case 3:assert(endpoint==4 && live[0]==0x22);m->m1_i1=CP32_APP_WAITPID;m->m1_i2=102;m->m1_i3=1;break;
   case 4:assert(endpoint==4);m->m1_i1=CP32_APP_YIELD;break;
   case 5:assert(endpoint==5 && live[0]==0x33);m->m1_i1=CP32_APP_YIELD;break;
   case 6:assert(endpoint==4 && live[0]==0x22);m->m1_i1=CP32_APP_WAITPID;m->m1_i2=102;m->m1_p1=(char *)CP32_APP_DATA;break;
   case 7:assert(endpoint==5);m->m1_i1=CP32_APP_YIELD;break;
   case 8:assert(endpoint==5);p->user_time=2;p->sys_time=3;m->m_type=APP_EXIT;m->m1_i1=7;if(yielding==5)app_fault.signal=11;break;
   case 9:assert(endpoint==4 && live[0]==0x22);m->m1_i1=CP32_APP_YIELD;break;
   case 10:assert(endpoint==3 && live[0]==0xa5);m->m1_i1=CP32_APP_WAITPID;m->m1_i2=101;break;
   case 11:assert(endpoint==4 && live[0]==0x22);p->user_time=2;p->sys_time=3;m->m_type=APP_EXIT;break;
   default:assert(endpoint==3 && received==13);m->m_type=APP_EXIT;break;
  }return 0;
 }
 if(yielding){
  switch(received++){
   case 0:assert(endpoint==3);m->m_type=APP_FILE;m->m1_i1=CP32_APP_FORK;break;
   case 1:assert(endpoint==4);memset(live,0x22,sizeof(live));m->m_type=APP_FILE;m->m1_i1=CP32_APP_YIELD;break;
   case 2:assert(endpoint==3 && live[0]==0xa5);m->m_type=APP_FILE;m->m1_i1=CP32_APP_WAITPID;m->m1_i2=101;m->m1_i3=1;m->m1_p1=(char *)CP32_APP_DATA;break;
   case 3:assert(endpoint==3 && live[16384]==0xa5);memset(live,0x33,sizeof(live));m->m_type=APP_FILE;m->m1_i1=CP32_APP_YIELD;break;
   case 4:assert(endpoint==4 && live[0]==0x22);memset(live,0x44,sizeof(live));m->m_type=APP_FILE;m->m1_i1=CP32_APP_YIELD;break;
   case 5:assert(endpoint==3 && live[0]==0x33);m->m_type=APP_FILE;m->m1_i1=CP32_APP_WAITPID;m->m1_i2=101;m->m1_p1=(char *)CP32_APP_DATA;
    if(yielding>1){m->m_type=APP_EXIT;m->m1_i1=23;if(yielding==3)app_fault.signal=4;}break;
   case 6:assert(endpoint==4 && live[0]==0x44);m->m_type=APP_FILE;m->m1_i1=CP32_APP_YIELD;break;
   case 7:assert(endpoint==4);p->user_time=2;p->sys_time=3;m->m_type=APP_EXIT;m->m1_i1=7;break;
   default:assert(endpoint==3 && received==9);m->m_type=APP_EXIT;break;
  }return 0;
 }
 switch(received++){
 case 0:assert(endpoint==3);m->m_type=APP_FILE;m->m1_i1=CP32_APP_FORK;break;
 case 1:case 2:assert(endpoint==4);m->m_type=APP_FILE;m->m1_i1=received==2 ? CP32_APP_SETSID:CP32_APP_GETPGRP;break;
 case 3:assert(endpoint==4);memset(live,0x22,sizeof(live));m->m_type=APP_FILE;m->m1_i1=CP32_APP_FORK;break;
 case 4:assert(endpoint==5);memset(live,0x33,sizeof(live));p->user_time=2;p->sys_time=3;m->m_type=APP_EXIT;m->m1_i1=9;if(faults)app_fault.signal=11;break;
 case 5:assert(endpoint==4 && cp32_app_state.pid==101 && live[0]==0x22);m->m_type=APP_FILE;m->m1_i1=CP32_APP_WAITPID;m->m1_i2=0;m->m1_p1=(char *)CP32_APP_DATA;break;
 case 6:assert(endpoint==4);p->user_time=2;p->sys_time=3;m->m_type=APP_EXIT;m->m1_i1=7;if(faults)app_fault.signal=4;break;
 case 7:assert(endpoint==3 && cp32_app_state.pid==100 && !memcmp(live,original,sizeof(live)));m->m_type=APP_FILE;m->m1_i1=CP32_APP_WAITPID;m->m1_i2=-101;m->m1_p1=(char *)CP32_APP_DATA;break;
 case 8:assert(endpoint==3);m->m_type=APP_FILE;m->m1_i1=CP32_APP_TIMES;m->m1_i3=20;m->m1_p1=(char *)CP32_APP_DATA;break;
 case 9:assert(endpoint==3);m->m_type=APP_FILE;m->m1_i1=CP32_APP_TIMES;m->m1_i3=20;m->m1_p1=(char *)(CP32_APP_TOP-1);break;
 default:assert(endpoint==3 && received==11);m->m_type=APP_EXIT;if(faults)app_fault.signal=4;break;
 }return 0;
}
static int send(int endpoint,message *m){
 if(yielding==13){
  static const int dest[]={3,3,3,3,3,4,4,3,3,3,4,3,3};
  static const int values[]={0,0,0,0,1<<14,0,0,101,0,0,0,101,1};
  assert(replied<13 && endpoint==dest[replied] && m->m_type==values[replied]);replied++;return 0;
 }
 if(yielding>=10){
  static const int dest[]={3,4,3,3,3,3,4,4,3,3,3};
  static const int values[]={0,0,101,0,0,0,0,1<<14,0,101,1<<14};
  const int bit=1<<(yielding==14 ? 9:14);
  const int self[]={0,0,bit,-22,bit};
  if(yielding==12 || yielding==14)assert(replied<5 && endpoint==3 && m->m_type==self[replied]);
  else {
   assert(replied<11 && endpoint==dest[replied] && m->m_type==values[replied]);
   if(replied==9){int status;memcpy(&status,live+16384,4);assert(status==15);}
  }
  replied++;return 0;
 }
 if(yielding==9){
  static const int values[]={0,-22,-22,-22,-3,-1};
  assert(replied<6 && endpoint==3 && m->m_type==values[replied]);replied++;return 0;
 }
 if(yielding>=6){
  static const int dest[3][7]={{4,3,3,3,3,3,3},{4,3,3},{4,5,4,4,4,3,3}};
  static const int value[3][7]={{0,101,0,0,0,101,-3},{0,101,101},{0,0,102,0,102,101,101}};
  int row=yielding-6;
  assert(replied<(yielding==7 ? 3:7) && endpoint==dest[row][replied] && m->m_type==value[row][replied]);
  if((yielding==6 && replied==5) || (yielding==7 && replied==2) || (yielding==8 && replied==4)) {
   int status;memcpy(&status,live+16384,4);assert(status==(yielding==6 ? 9:15));
  }
  replied++;return 0;
 }
 if(yielding>=4){
  static const int dest[]={4,5,4,4,5,4,5,5,4,3,4,3};
  static const int values[]={0,0,102,0,0,0,0,0,102,101,0,101};
  assert(replied<12 && endpoint==dest[replied] && m->m_type==values[replied]);
  if(replied==8){int status;memcpy(&status,live+16384,4);assert(status==(yielding==5 ? 11:7<<8));}
  replied++;return 0;
 }
 if(yielding){
  static const int values[]={0,101,0,0,0,0,0,101},dest[]={4,3,3,4,3,4,4,3};
  assert(replied<8 && m->m_type==values[replied] && endpoint==dest[replied]);
  if(replied==7){int status;memcpy(&status,live+16384,4);assert(status==(7<<8));}
  replied++;return 0;
 }
 static const int results[]={0,101,101,0,102,102,101,101,0,-14};
 static const int endpoints[]={4,4,4,5,4,4,3,3,3,3};
 assert(!locked && replied<10 && endpoint==endpoints[replied]);
 assert(m->m_type==results[replied]);
 if(replied==5 || replied==7){int status;memcpy(&status,live+16384,4);assert(status==(faults ? (replied==5 ? 11:4):((replied==5 ? 9:7)<<8)));}
 if(replied==8 || replied==9){struct cp32_app_times t;memcpy(&t,live+16384,sizeof(t));assert(t.user==0 && t.system==0 && t.children_user==4 && t.children_system==6 && t.uptime==1234);}
 replied++;return 0;
}
'''
fs=f.fs+'\nstatic void cp32_shell_app_files_reset(void){cp32_fproc_exit(&cp32_app_state);}\n'
functions='\n'.join(extract_function(f.root/'src/kernel/process.c',n) for n in ('cp32_proc_reserve','cp32_proc_publish'))
functions+=extract_function(f.root/'src/kernel/context.c','cp32_exec_frame')
functions+=extract_function(f.root/'src/kernel/application.c','cp32_trace_app_fault')
functions+=extract_function(f.root/'src/kernel/application.c','application_fault_take')
functions+=extract_function(f.root/'src/kernel/application.c','application_times').replace('memcpy((void *)p,&t,sizeof(t));','memcpy(live+16384+p-CP32_APP_DATA,&t,sizeof(t));')
run=extract_function(f.root/'src/kernel/application.c','cp32_application_run')
run=run.replace('__asm__ volatile("memw\\n\\tisync" ::: "memory");','syncs++;')
run=run.replace('memset((void *)old,0,heap_break-old);','memset(live+16384+old-CP32_APP_DATA,0,heap_break-old);')
run=run.replace('memset((void *)CP32_APP_STACK,0,CP32_APP_TOP-CP32_APP_STACK);','/* fake loader supplies backing bytes */')
test=r'''
int main(int argc,char **argv){
 (void)argv;faults=argc==2;yielding=argc>2 ? argc-2:0;
 procs[1].p_pid=1;
 for(int n=3;n<NR_PROCS;n++){procs[n].p_nr=n;procs[n].p_flags=P_SLOT_FREE;}
 const char *args[]={"hello"};int status=-1;
 assert(!cp32_application_run(0,0,0,1,args,output,&status));
 assert(status==(yielding==14 ? 138:yielding==12 ? 143:yielding==9 ? 137:yielding==3 || faults ? 132:yielding==2 ? 23:0) && !app_fault.signal && !app_busy && app_endpoint==-1);
 assert(received==(yielding==13 ? 14:yielding>=10 ? ((yielding==12 || yielding==14) ? 6:12):yielding>=6 ? (yielding==9 ? 7:yielding==7 ? 4:8):yielding>=4 ? 13:yielding ? (yielding>1 ? 8:9):11) && replied==(yielding==13 ? 13:yielding>=10 ? ((yielding==12 || yielding==14) ? 5:11):yielding>=6 ? (yielding==9 ? 6:yielding==7 ? 3:7):yielding>=4 ? 12:yielding ? (yielding>1 ? 7:8):10));
 assert(!allocated && allocs==frees && !refs && !cp32_fproc_suspended);
 assert(procs[3].p_flags==P_SLOT_FREE && procs[4].p_flags==P_SLOT_FREE && procs[5].p_flags==P_SLOT_FREE);
 assert(procs[1].child_utime==(yielding>=6 ? 0:yielding && yielding<4 ? 2:4) && procs[1].child_stime==(yielding>=6 ? 0:yielding && yielding<4 ? 3:6));
 puts("fork broker: two nested replies/restores, private waits, descendant accounting and final reap pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+extra+f.proc+fs+f.bridge+f.source+functions+run+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-pointer-to-int-cast',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
 subprocess.run([str(p/'test'),'faults'],check=True)
 for n in range(2,16):subprocess.run([str(p/'test')]+['yield']*n,check=True)
