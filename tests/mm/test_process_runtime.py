"""Production concurrent MM policy with bounded physical RAM and modeled IPC/FS."""
from pathlib import Path
import subprocess,tempfile,re,sys
root=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
allocator=(root/'src/mm/alloc.c').read_text().replace('#include "mm.h"','')
prepare_return=extract_function(root/'src/kernel/proc.c','cp32_prepare_return')
header=(root/'src/kernel/app_process.h').read_text()
header='\n'.join(x for x in header.splitlines() if not x.startswith('#include'))
header=header.replace('(CP32_APP_TEXT-0x6f0000U)','((uintptr_t)image)')
source=(root/'src/mm/process.c').read_text()
source='\n'.join(x for x in source.splitlines() if not x.startswith('#include'))
# Model the instruction-side view separately from the writable SRAM alias.
barrier = '#if defined(__XTENSA__)\n  __asm__ volatile("memw\\n\\tisync" ::: "memory");\n#endif'
assert source.count(barrier) == 1
source=source.replace(barrier,'  assert(lock_depth); fetched_byte=image[0]; sync_count++;')
source=re.sub(r'\(void \*\)\(uintptr_t\)\(([^()]+)<<CLICK_SHIFT\)',r'ram(\1)',source)
source=source.replace('(struct cp32_fproc *)(uintptr_t)((base<<CLICK_SHIFT)+CP32_APP_IMAGE_BYTES)','(struct cp32_fproc *)(ram(base)+CP32_APP_IMAGE_BYTES)')
source=source.replace('cp32_mem_alloc(', 'runtime_alloc(').replace('cp32_mem_free(', 'runtime_free(')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "abi.h"
#include "launch_trace.h"
#define CP32_IRAM_EXT
#define OK 0
#define NO_NUM 0
#define LOW_USER 2
#define NR_PROCS 12
#define MM_PROC_NR 0
#define FS_PROC_NR 1
#define HARDWARE -1
#define HARD_INT 2
#define CLICK_SHIFT 12
#define NR_TASKS 9
#define PUBLIC
#define TRUE 1
#define FALSE 0
#define EINVAL (-22)
#define EACCES (-13)
#define P_SLOT_FREE 1
#define NO_MAP 2
#define RECEIVING 8
#define P_ZOMBIE 128
#define ANY 132
#define D 1
#define S 2
#define CP32_APP_TEXT 0x403d8000U
#define CP32_APP_TEXT_END 0x403dc000U
#define CP32_APP_DATA 0x3fcec000U
#define CP32_APP_STACK 0x3fcef000U
#define CP32_APP_TOP 0x3fcf0000U
#define NIL_PROC ((struct proc *)0)
typedef uintptr_t phys_bytes;
typedef uintptr_t vir_bytes;
struct proc;
struct cp32_fork_state;
typedef struct {int m_type,m_source,m1_i1,m1_i2,m1_i3;char *m1_p1;} message;
struct stackframe_s {uintptr_t a[16],pc,sp,psw;};
struct cp32_fproc {struct cp32_fork_state *lifecycle;unsigned pid,uid,euid;int fds[4];};
struct proc {
 int p_nr,p_pid,p_flags,p_parent_pid,p_app_group,p_exitstatus,p_getfrom,p_sendto;
 unsigned p_app_pending,p_app_sigmask,p_app_ignore,p_shadow,p_heap_floor,p_heap_break;
 unsigned p_zombie_uid,p_zombie_euid;
 unsigned user_time,sys_time,child_utime,child_stime;
 struct cp32_fproc *p_files;struct stackframe_s p_reg;
 struct {unsigned mem_vir,mem_phys,mem_len;} p_map[3];
 message *p_messbuf;int p_blocked_frame_valid;
 unsigned p_blocked_frame_pc,p_blocked_frame_psw,p_blocked_frame_sp;
 char p_name[16];
};
static struct proc procs[NR_PROCS],*current_proc;
#define proc_addr(n) (procs+(n))
static int lock_depth,fail_alloc,fail_clone,fail_fs,closed,refs,polls,read_result=-11;
static int pin_count;static struct proc *pin;
static message replies[NR_PROCS];static unsigned reply_count[NR_PROCS];
/* Image 205 updater: 39 whole 4 KiB heap pages, FS takes the first two. */
enum { UPDATER_BASE=0x3fca9, UPDATER_PAGES=0x3fcd0-UPDATER_BASE, LARGE_PAGES=74 };
typedef uint32_t phys_clicks;
static struct {phys_clicks base,size;} mem[2]={{0,0},{UPDATER_BASE,UPDATER_PAGES}};
static union {uint64_t align;unsigned char bytes[LARGE_PAGES<<CLICK_SHIFT];} heap;
static unsigned char image[32768];
static int fetched_byte=-1;static unsigned sync_count;
static struct {enum cp32_launch_step step;unsigned value;} launch_events[8];
static unsigned launch_event_count,exit_events,last_exit;
static unsigned fork_slot;
static void run_first_return(void);
static int first_return_pending;

void cp32_trace_launch(enum cp32_launch_step step,unsigned value){
 if(step==LAUNCH_FORK){fork_slot=value;return;}
 if(step==LAUNCH_EXIT){exit_events++;last_exit=value;return;}
 if(step==LAUNCH_ACCEPT)launch_event_count=0;
 assert(launch_event_count<sizeof(launch_events)/sizeof(launch_events[0]));
 launch_events[launch_event_count].step=step;launch_events[launch_event_count++].value=value;
 if(step==LAUNCH_PUBLISH)first_return_pending=1;
}
void cp32_trace_swap_begin(void){assert(lock_depth);}
void cp32_trace_swap_end(void){assert(lock_depth);}
static void expect_event(unsigned index,enum cp32_launch_step step,unsigned value){
 assert(index<launch_event_count && launch_events[index].step==step && launch_events[index].value==value);
}
static unsigned char *ram(unsigned base){
 assert(base>=mem[1].base && base<mem[1].base+mem[1].size);
 return heap.bytes+((base-mem[1].base)<<CLICK_SHIFT);
}
static unsigned char *app(unsigned offset){return image+16384+offset;}
static int lock_save(void){return lock_depth++;}
static void restore_lock(int n){
 assert(lock_depth==n+1);lock_depth=n;
 if(n==0 && first_return_pending){
  first_return_pending=0;
  int irq=lock_save();
  run_first_return();
  restore_lock(irq);
 }
}
static void panic(const char *p,int n){(void)n;fprintf(stderr,"%s\n",p);assert(0);}
static struct proc *cp32_proc_reserve(int pid){assert(lock_depth);for(int i=3;i<NR_PROCS;i++)if(procs[i].p_flags==P_SLOT_FREE){memset(procs+i,0,sizeof(procs[i]));procs[i].p_nr=i;procs[i].p_pid=pid;procs[i].p_flags=NO_MAP;return procs+i;}return 0;}
static int cp32_proc_release(struct proc *p){assert(lock_depth && !p->p_shadow);int n=p->p_nr;memset(p,0,sizeof(*p));p->p_nr=n;p->p_flags=P_SLOT_FREE;return 0;}
static int cp32_proc_clone(int a,int b,int pid,int broker){assert(lock_depth && broker==MM_PROC_NR);if(fail_clone)return -22;assert(procs[a].p_flags==RECEIVING && procs[b].p_flags==P_SLOT_FREE);procs[b]=procs[a];procs[b].p_nr=b;procs[b].p_pid=pid;procs[b].p_parent_pid=procs[a].p_pid;procs[b].p_files=0;procs[b].p_shadow=0;procs[b].p_flags=NO_MAP|RECEIVING;procs[b].user_time=procs[b].sys_time=procs[b].child_utime=procs[b].child_stime=0;return 0;}
static int cp32_proc_publish(struct proc *p){assert(lock_depth && p->p_flags==NO_MAP && fetched_byte==image[0]);p->p_flags=0;return 0;}
static int cp32_exec_frame(struct proc *p,unsigned pc,unsigned sp,unsigned arg){memset(&p->p_reg,0,sizeof(p->p_reg));p->p_reg.pc=pc;p->p_reg.sp=sp;p->p_reg.a[1]=p->p_reg.a[15]=sp;p->p_reg.a[2]=arg;return 0;}
static void lock_ready(struct proc *p){assert(lock_depth && !p->p_flags);}
static unsigned get_uptime(void){return 456;}
static phys_bytes numap(int nr,vir_bytes address,unsigned size){if(nr<=LOW_USER)return address;if(address<CP32_APP_DATA || address>=CP32_APP_TOP || size>CP32_APP_TOP-address)return 0;return procs[nr].p_shadow ? (uintptr_t)(ram(procs[nr].p_shadow)+16384+address-CP32_APP_DATA):(uintptr_t)(image+16384+address-CP32_APP_DATA);}
static int send(int nr,message *m){assert(nr>=0 && nr<NR_PROCS);replies[nr]=*m;reply_count[nr]++;if(procs[nr].p_flags==RECEIVING)procs[nr].p_flags=0;return 0;}
'''
helpers=r'''
static unsigned runtime_alloc(unsigned pages,int owner){
 assert(lock_depth && owner==MM_PROC_NR);
 return fail_alloc ? 0:cp32_mem_alloc(pages,owner);
}
static int runtime_free(unsigned base,int owner){
 assert(lock_depth && owner==MM_PROC_NR);
 return cp32_mem_free(base,owner);
}
static int allocated_blocks(void){
 int count=0;
 for(int i=0;i<CP32_MAX_MEM_BLOCKS;i++)
  if(cp32_mem_blocks[i].used && cp32_mem_blocks[i].owner==MM_PROC_NR)count++;
 return count;
}
#define allocated allocated_blocks()
unsigned cp32_application_window_busy(void){return pin_count;}
struct proc *cp32_application_window_owner(void){return pin;}
int cp32_shell_clone_launch(struct cp32_fproc *f,unsigned pid){if(fail_fs)return -24;f->pid=pid;f->fds[0]=1;f->lifecycle=0;refs++;return 0;}
void cp32_runtime_select_fs(struct proc *p){(void)p;}
int cp32_runtime_signal_request(struct proc *p,message *m,int *r){(void)p;(void)m;(void)r;return 0;}
void cp32_runtime_signal_fork(struct proc *c,struct proc *p){memcpy(cp32_runtime_state(c)->handlers,cp32_runtime_state(p)->handlers,sizeof(cp32_runtime_state(c)->handlers));c->p_app_pending=0;}
void cp32_runtime_signal_exec(struct proc *p){memset(cp32_runtime_state(p)->handlers,0,sizeof(cp32_runtime_state(p)->handlers));}
void cp32_runtime_signal_return(struct proc *p){(void)p;}
int _sendrec(int nr,message *m){assert(nr==FS_PROC_NR);int result=0;
 if(m->m_type==CP32_MM_FS_CLONE){struct proc *parent=procs+m->m1_i1,*child=procs+m->m1_i2;
  if(fail_fs)result=-24;else {struct cp32_fork_state *s=child->p_files->lifecycle;*child->p_files=*parent->p_files;child->p_files->lifecycle=s;child->p_files->pid=child->p_pid;refs++;}}
 else if(m->m_type==CP32_MM_FS_EXIT){struct proc *p=procs+m->m1_i1;assert(refs && p->p_files->fds[0]);refs--;closed++;p->p_files->fds[0]=0;p->p_files->pid=0;}
 else if(m->m_type==CP32_MM_FS_READ){assert(cp32_runtime_read_pending() && !cp32_runtime_needs_tick());polls++;result=read_result;if(result>0)memset((void *)numap(m->m1_i1,(uintptr_t)m->m1_p1,result),'x',result);}
 else if(m->m_type==CP32_MM_FS_EXEC){struct cp32_runtime_exec *s=(void *)m->m1_p1;if(fail_fs)result=-8;else {memset(ram(s->backing),0x61,CP32_APP_IMAGE_BYTES);s->entry=CP32_APP_TEXT+4;s->sp=CP32_APP_TOP-32;s->heap=CP32_APP_DATA+512;strcpy(s->name,"new");}}
 else assert(0);
 m->m_source=FS_PROC_NR;m->m_type=result;return 0;
}
'''
test=r'''
/* Reproduce the hardware ordering: the first exception return occurs once
 * the process is published, before the FS launch reply. */
static void run_first_return(void){
 assert(lock_depth && resident && resident->p_flags==0);
 assert(cp32_mem_blocks[0].owner==FS_PROC_NR && cp32_mem_blocks[0].used);
 unsigned base=cp32_runtime_state(resident)->backing;
 assert(base==mem[1].base+2 && cp32_mem_blocks[1].base==base);
 cp32_prepare_return(resident);
}
static void boot(void){
 for(int i=0;i<NR_PROCS;i++){memset(procs+i,0,sizeof(procs[i]));procs[i].p_nr=i;procs[i].p_flags=P_SLOT_FREE;}
 procs[0].p_flags=0;procs[1].p_flags=RECEIVING;procs[1].p_pid=2;
 procs[2].p_flags=RECEIVING;procs[2].p_pid=1;current_proc=procs;
}
static message request(struct proc *p,int type,int op,int fd,char *buffer,unsigned count){
 int lock=lock_save();if(p->p_nr>LOW_USER)assert(!cp32_runtime_activate(p));restore_lock(lock);
 p->p_flags=RECEIVING;p->p_getfrom=MM_PROC_NR;p->p_messbuf=(void *)(uintptr_t)(CP32_APP_DATA+1024);
 message m={type,p->p_nr,op,fd,(int)count,buffer};return m;
}
static struct proc *start(void){
 struct cp32_runtime_launch q={CP32_APP_TEXT,CP32_APP_TOP-32,CP32_APP_DATA+256,1234,"hello"};
 unsigned before_sync=sync_count;fetched_byte=-1;
 memset((void *)CP32_APP_IMAGE_BASE,0x17,CP32_APP_IMAGE_BYTES);
 message m={CP32_MM_LAUNCH,FS_PROC_NR,0,0,0,(char *)&q};assert(cp32_runtime_dispatch(&m));
 assert(sync_count==before_sync+1 && fetched_byte==0x17);
 assert(!replies[1].m_type && live_apps==1 && allocated==1);
 struct proc *p=procs+replies[1].m1_i1;
 assert(launch_event_count==7);
 expect_event(0,LAUNCH_ACCEPT,q.sp);expect_event(1,LAUNCH_ALLOC,cp32_runtime_state(p)->backing);
 expect_event(2,LAUNCH_SLOT,p->p_nr);expect_event(3,LAUNCH_FILES,0);
 expect_event(4,LAUNCH_PUBLISH,p->p_pid);expect_event(5,LAUNCH_RETURN,q.entry);
 expect_event(6,LAUNCH_REPLY,0);return p;
}
static struct proc *child(struct proc *p){message m=request(p,APP_FILE,CP32_APP_FORK,0,0,0);assert(cp32_runtime_dispatch(&m));int pid=replies[p->p_nr].m_type;assert(pid>0);struct proc *c=by_pid(pid);assert(c && !c->p_flags && !replies[c->p_nr].m_type && fork_slot==(unsigned)c->p_nr);return c;}
static void finish(struct proc *p,int code){
 unsigned before=exit_events;message m=request(p,APP_EXIT,code,0,0,0);
 assert(cp32_runtime_dispatch(&m));assert(exit_events==before+1 && last_exit==(unsigned)code);
}
static void init_wait(void){static int status;message m=request(procs+2,APP_FILE,CP32_APP_WAITPID,-1,(char *)&status,0);assert(cp32_runtime_dispatch(&m));}
int main(int argc,char **argv){
 int max_children=3;
 if(argc==2 && !strcmp(argv[1],"large")){
  /* Keep the original eight-live-process stress case on a larger modeled heap. */
  mem[1].base=0x3fca0;mem[1].size=LARGE_PAGES;max_children=7;
 }
 assert(cp32_mem_alloc(2,FS_PROC_NR)==mem[1].base);
 boot();init_wait();assert(init_waiting);
 struct cp32_runtime_launch badlaunch={CP32_APP_TEXT,CP32_APP_TOP-32,CP32_APP_DATA+256,1234,"hello"};
 message bad={CP32_MM_LAUNCH,FS_PROC_NR,0,0,0,(char *)&badlaunch};
 fail_alloc=1;assert(cp32_runtime_dispatch(&bad) && replies[1].m_type==-12 && !allocated && !refs);fail_alloc=0;
 assert(launch_event_count==3);expect_event(1,LAUNCH_ALLOC,0);expect_event(2,LAUNCH_REPLY,(unsigned)-12);
 bad.m_type=CP32_MM_LAUNCH;fail_fs=1;assert(cp32_runtime_dispatch(&bad) && replies[1].m_type==-24 && !allocated && !refs);fail_fs=0;
 assert(launch_event_count==5);expect_event(3,LAUNCH_FILES,(unsigned)-24);expect_event(4,LAUNCH_REPLY,(unsigned)-24);

 for(int round=0;round<8;round++) {
  struct proc *p=start();int pn=p->p_nr,pid=p->p_pid;
  int verify_lock=lock_save();struct cp32_fork_state *state=cp32_runtime_state(p);
  struct cp32_fproc *files=p->p_files;unsigned backing=state->backing;
  p->p_files=0;assert(cp32_runtime_activate(p)==-22);p->p_files=files;
  state->owner=procs;assert(cp32_runtime_activate(p)==-22);state->owner=p;
  state->backing=0;assert(cp32_runtime_activate(p)==-22);
  state->backing=mem[1].base;assert(cp32_runtime_activate(p)==-22); /* FS owns this block. */
  state->backing=backing+2;assert(cp32_runtime_activate(p)==-22); /* only seven pages left */
  state->backing=backing;
  p->p_shadow=backing;assert(cp32_runtime_activate(p)==-22);p->p_shadow=0;
  assert(!cp32_runtime_activate(procs) && !cp32_runtime_activate(procs+LOW_USER));restore_lock(verify_lock);
  fail_alloc=1;message m=request(p,APP_FILE,CP32_APP_FORK,0,0,0);assert(cp32_runtime_dispatch(&m));assert(replies[pn].m_type==-12 && allocated==1 && refs==1);fail_alloc=0;
  fail_fs=1;m=request(p,APP_FILE,CP32_APP_FORK,0,0,0);assert(cp32_runtime_dispatch(&m));assert(replies[pn].m_type==-24 && allocated==1 && refs==1);fail_fs=0;
  struct proc *a=child(p);
  verify_lock=lock_save();state->backing=0;
  assert(cp32_runtime_activate(a)==-22 && resident==p && !p->p_shadow && a->p_shadow);
  state->backing=backing;restore_lock(verify_lock);
  m=request(p,APP_FILE,CP32_APP_FORK,0,0,0);int snapshot_lock=lock_save();
  assert(!cp32_runtime_activate(a));restore_lock(snapshot_lock);assert(p->p_shadow);
  assert(cp32_runtime_dispatch(&m));struct proc *b=by_pid(replies[pn].m_type);assert(b && !b->p_flags);
  int apid=a->p_pid,bpid=b->p_pid;
  assert(live_apps==3 && allocated==3 && a->p_parent_pid==pid && b->p_parent_pid==pid);
  assert(!cp32_runtime_needs_tick()); /* CPU-only siblings need no MM polling. */
  m=request(b,APP_FILE,CP32_APP_SETSID,0,0,0);assert(cp32_runtime_dispatch(&m) && replies[b->p_nr].m_type==bpid);
  m=request(b,APP_FILE,CP32_APP_SETSID,0,0,0);assert(cp32_runtime_dispatch(&m) && replies[b->p_nr].m_type==-1);
  m=request(p,APP_FILE,CP32_APP_WAITPID,-bpid,0,1);assert(cp32_runtime_dispatch(&m) && !replies[pn].m_type);
  m=request(p,APP_FILE,CP32_APP_WAITPID,0,0,1);assert(cp32_runtime_dispatch(&m) && !replies[pn].m_type);
  m=request(p,APP_FILE,CP32_APP_WAITPID,-30001,0,0);assert(cp32_runtime_dispatch(&m) && replies[pn].m_type==-10);
  m=request(p,APP_FILE,CP32_APP_WAITPID,apid,0,2);assert(cp32_runtime_dispatch(&m) && replies[pn].m_type==-22);
  int lock=lock_save();assert(!cp32_runtime_activate(a));*app(0)=0x42;assert(!cp32_runtime_activate(p));assert(*app(0)==0x17);*app(0)=0x31;
  pin=p;pin_count=1;assert(cp32_runtime_activate(a)==-16 && !cp32_runtime_can_run(a) && cp32_runtime_can_run(p));pin_count=0;pin=0;
  assert(!cp32_runtime_activate(a) && *app(0)==0x42);restore_lock(lock);
  m=request(p,APP_FILE,CP32_APP_WAITPID,apid,(char *)(uintptr_t)(CP32_APP_DATA+64),1);assert(cp32_runtime_dispatch(&m));assert(!replies[pn].m_type);
  p->p_app_pending=1;m=request(p,APP_FILE,CP32_APP_WAITPID,apid,0,0);assert(cp32_runtime_dispatch(&m));assert(replies[pn].m_type==-4 && !cp32_runtime_state(p)->waiting);
  p->p_app_sigmask=1;m=request(p,APP_FILE,CP32_APP_WAITPID,apid,0,0);assert(cp32_runtime_dispatch(&m));assert(cp32_runtime_state(p)->waiting==CP32_RUNTIME_WAIT);cp32_runtime_interrupt(p);p->p_app_pending=p->p_app_sigmask=0;
  m=request(p,APP_FILE,CP32_APP_WAITPID,apid,(char *)(uintptr_t)(CP32_APP_DATA+64),0);unsigned before=reply_count[pn];assert(cp32_runtime_dispatch(&m));assert(reply_count[pn]==before && cp32_runtime_state(p)->waiting==CP32_RUNTIME_WAIT);
  assert(!cp32_runtime_needs_tick()); /* Child exits explicitly wake waitpid. */
  cp32_runtime_interrupt(p);assert(replies[pn].m_type==-4 && !cp32_runtime_state(p)->waiting);
  assert(!cp32_runtime_read_pending());m=request(p,APP_READ,2,0,(char *)(uintptr_t)(CP32_APP_DATA+128),0);assert(cp32_runtime_dispatch(&m));assert(cp32_runtime_state(p)->waiting==CP32_RUNTIME_READ && cp32_runtime_read_pending());
  assert(cp32_runtime_needs_tick());
  m=request(b,APP_READ,1,0,(char *)(uintptr_t)(CP32_APP_DATA+200),0);assert(cp32_runtime_dispatch(&m));
  cp32_runtime_interrupt(p);
  assert(replies[pn].m_type==-4 && cp32_runtime_needs_tick());
  /* A later keyboard byte completes the remaining reader on the real MM
   * HARD_INT path, without another user request. */
  int oldpoll=polls;read_result=1;
  message tick={HARD_INT,HARDWARE,0,0,0,0};assert(cp32_runtime_dispatch(&tick));
  assert(polls==oldpoll+1 && replies[b->p_nr].m_type==1 && *app(200)=='x');
  assert(!cp32_runtime_read_pending() && !cp32_runtime_needs_tick());
  oldpoll=polls;read_result=-11;cp32_runtime_tick();assert(polls==oldpoll);
  p->p_app_pending=1;m=request(p,APP_READ,2,0,(char *)(uintptr_t)(CP32_APP_DATA+128),0);assert(cp32_runtime_dispatch(&m));assert(polls==oldpoll && replies[pn].m_type==-4 && !cp32_runtime_state(p)->waiting);p->p_app_pending=0;
  read_result=2;m=request(p,APP_READ,2,0,(char *)(uintptr_t)(CP32_APP_DATA+128),0);assert(cp32_runtime_dispatch(&m));assert(replies[pn].m_type==2);read_result=-11;
  a->p_files->uid=123;a->p_files->euid=456;
  finish(a,7);assert(by_pid(apid)->p_flags==P_ZOMBIE && allocated==2);
  assert(by_pid(apid)->p_zombie_uid==123 && by_pid(apid)->p_zombie_euid==456);
  m=request(p,APP_FILE,CP32_APP_WAITPID,apid,(char *)1,0);assert(cp32_runtime_dispatch(&m));assert(replies[pn].m_type==-14 && by_pid(apid)->p_flags==P_ZOMBIE);
  /* Root exits with an uncollected zombie AND an independently live sibling. */
  finish(p,9);assert(b->p_parent_pid==1 && !by_pid(apid) && allocated==1 && refs==1);
  if(!round)for(unsigned i=0;i<29902;i++)assert(new_pid()!=pid);
  assert(replies[2].m_type==apid);init_wait();assert(init_waiting);
  finish(b,0);assert(!live_apps && !allocated && !refs && !by_pid(bpid));
  assert(replies[1].m_type==CP32_MM_JOB_DONE && replies[1].m1_i1==9);init_wait();assert(init_waiting);
 }
 /* The last live root can leave multiple adopted zombies. Completion waits
  * for INIT's next request, even though no live process can generate ticks. */
 struct proc *oroot=start(),*oa=child(oroot),*ob=child(oroot);
 int oapid=oa->p_pid,obpid=ob->p_pid;finish(oa,1);finish(ob,2);
 unsigned completed_before=reply_count[FS_PROC_NR];finish(oroot,12);
 assert(!live_apps && !allocated && !refs && root_pid && root_exited);
 assert(!by_pid(oapid) && by_pid(obpid)->p_flags==P_ZOMBIE);
 assert(replies[LOW_USER].m_type==oapid && reply_count[FS_PROC_NR]==completed_before);
 assert(!cp32_runtime_needs_tick());init_wait();
 assert(!by_pid(obpid) && !root_pid && !root_exited);
 assert(replies[LOW_USER].m_type==obpid && replies[FS_PROC_NR].m_type==CP32_MM_JOB_DONE && replies[FS_PROC_NR].m1_i1==12);
 assert(reply_count[FS_PROC_NR]==completed_before+1);init_wait();assert(init_waiting);
 /* Bounded physical-image exhaustion returns ENOMEM and reuse releases every block. */
 struct proc *full=start(),*children[7];
 for(int i=0;i<max_children;i++)children[i]=child(full);
 assert(allocated==max_children+1 && refs==allocated);message exhausted=request(full,APP_FILE,CP32_APP_FORK,0,0,0);
 assert(cp32_runtime_dispatch(&exhausted) && replies[full->p_nr].m_type==-12 && allocated==max_children+1);
 for(int i=0;i<max_children;i++){int pid=children[i]->p_pid;finish(children[i],i);message w=request(full,APP_FILE,CP32_APP_WAITPID,pid,0,0);assert(cp32_runtime_dispatch(&w) && replies[full->p_nr].m_type==pid);}
 finish(full,0);assert(!allocated && !refs);
 /* Failed exec preserves state; successful exec preserves PID and changes frame. */
 struct proc *p=start();int pid=p->p_pid;struct cp32_app_exec_request *q=(void *)app(2048);memset(q,0,sizeof(*q));strcpy(q->path,"/boot/hello");q->argc=1;
 fail_fs=1;message m=request(p,APP_FILE,CP32_APP_EXEC,0,(char *)(uintptr_t)(CP32_APP_DATA+2048),sizeof(*q));assert(cp32_runtime_dispatch(&m));assert(replies[p->p_nr].m_type==-8 && allocated==1 && p->p_pid==pid && p->p_reg.pc==CP32_APP_TEXT);fail_fs=0;
 m=request(p,APP_FILE,CP32_APP_EXEC,0,(char *)(uintptr_t)(CP32_APP_DATA+2048),sizeof(*q));assert(cp32_runtime_dispatch(&m));assert(p->p_pid==pid && p->p_reg.pc==CP32_APP_TEXT+4 && p->p_heap_floor==CP32_APP_DATA+512 && allocated==1);
 int saved=lock_save();unsigned before_sync=sync_count;
 assert(!cp32_runtime_activate(p) && sync_count==before_sync+1 && fetched_byte==0x61);
 assert(!cp32_runtime_activate(p) && sync_count==before_sync+1);restore_lock(saved);
 finish(p,0);assert(!allocated && !refs && !live_apps && !lock_depth);
 puts("MM runtime: first return with FS-first heap, concurrent siblings, private images, waits/EINTR, orphans/INIT, exec rollback and eight cleanup cycles PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+allocator+header+helpers+source+prepare_return+test)
 subprocess.run(['cc','-std=c99','-D_DARWIN_C_SOURCE','-Wall','-Wextra','-Werror','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast','-fsanitize=undefined','-I'+str(root/'src/apps/hello'),'-iquote',str(root/'src/kernel'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 for geometry in ("updater","large"):
  subprocess.run([str(p/'test'),geometry],check=True)
