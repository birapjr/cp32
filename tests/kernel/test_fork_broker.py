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
#define CP32_APP_FORK 40
#define CP32_APP_WAITPID 41
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
static int application_replace(struct proc *p,message *m,uint32_t *fl,uint32_t *br){(void)p;(void)m;(void)fl;(void)br;return -8;}
static int application_file_request(message *m){(void)m;return -22;}
static int application_break(uint32_t f,uint32_t *b,int n){(void)f;(void)b;(void)n;return -1;}
static void output(const char *p,unsigned n){(void)p;(void)n;assert(0);}
static int receive(int endpoint,message *m){
 memset(m,0,sizeof(*m));struct proc *p=proc_addr(endpoint);
 p->p_flags=RECEIVING;p->p_getfrom=FS_PROC_NR;p->p_messbuf=m;
 switch(received++){
 case 0:case 2:assert(endpoint==3);m->m_type=APP_FILE;m->m1_i1=CP32_APP_FORK;break;
 case 1:case 3:assert(endpoint==4);memset(live,0x66,sizeof(live));p->user_time=2;p->sys_time=3;m->m_type=APP_EXIT;m->m1_i1=received==2 ? 7:9;break;
 case 4:case 5:assert(endpoint==3 && cp32_app_state.pid==100);m->m_type=APP_FILE;m->m1_i1=CP32_APP_WAITPID;m->m1_i2=received==5 ? 102:-1;m->m1_p1=(char *)CP32_APP_DATA;break;
 default:assert(endpoint==3 && received==7);m->m_type=APP_EXIT;break;
 }return 0;
}
static int send(int endpoint,message *m){
 static const int results[]={0,101,0,102,102,101};
 assert(!locked && replied<6);
 assert(endpoint==((replied==0 || replied==2) ? 4:3));
 assert(m->m_type==results[replied]);
 if(replied>=4){int status;memcpy(&status,live+16384,4);assert(status==((replied==4 ? 9:7)<<8));}
 replied++;return 0;
}
'''
fs=f.fs+'\nstatic void cp32_shell_app_files_reset(void){cp32_fproc_exit(&cp32_app_state);}\n'
functions='\n'.join(extract_function(f.root/'src/kernel/process.c',n) for n in ('cp32_proc_reserve','cp32_proc_publish'))
functions+=extract_function(f.root/'src/kernel/context.c','cp32_exec_frame')
run=extract_function(f.root/'src/kernel/application.c','cp32_application_run')
run=run.replace('__asm__ volatile("memw\\n\\tisync" ::: "memory");','syncs++;')
run=run.replace('memset((void *)old,0,heap_break-old);','memset(live+16384+old-CP32_APP_DATA,0,heap_break-old);')
run=run.replace('memset((void *)CP32_APP_STACK,0,CP32_APP_TOP-CP32_APP_STACK);','/* fake loader supplies backing bytes */')
test=r'''
int main(void){
 procs[1].p_pid=1;
 for(int n=3;n<NR_PROCS;n++){procs[n].p_nr=n;procs[n].p_flags=P_SLOT_FREE;}
 const char *args[]={"hello"};int status=-1;
 assert(!cp32_application_run(0,0,0,1,args,output,&status));
 assert(status==0 && received==7 && replied==6 && !app_busy && app_endpoint==-1);
 assert(!allocated && allocs==frees && !refs && !cp32_fproc_suspended);
 assert(procs[3].p_flags==P_SLOT_FREE && procs[4].p_flags==P_SLOT_FREE);
 assert(procs[1].child_utime==4 && procs[1].child_stime==6);
 puts("fork broker: child zero reply, parent PID reply, wait status, final reap and accounting pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+extra+f.proc+fs+f.bridge+f.source+functions+run+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-pointer-to-int-cast',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
