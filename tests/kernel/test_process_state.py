"""Persistent parent/heap metadata through production clone/swap/release."""
from pathlib import Path
import subprocess,tempfile,sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
import test_fork as f
setup=f.test[:f.test.index('int main(void)')]
checks=r'''
int main(void){
 for(int round=0;round<20;round++) {
  setup();cp32_fork_bind(procs+3);
  struct proc *active=procs+3;active->p_parent_pid=1;active->p_heap_floor=16;active->p_heap_break=32;
  fail_dup=1;assert(cp32_fork_start(&active,200)==-24);fail_dup=0;
  assert(active==procs+3 && active->p_parent_pid==1 && active->p_heap_break==32);
  assert(!cp32_fork_start(&active,200));
  assert(active==procs+4 && active->p_parent_pid==100 && active->p_heap_floor==16 && active->p_heap_break==32);
  active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
  active->p_heap_break=80;
  assert(procs[3].p_heap_break==32 && procs[3].p_parent_pid==1);
  message request={2007,0,45};
  assert(cp32_fork_handoff(&active,&request)==200);
  assert(active==procs+3 && active->p_parent_pid==1 && active->p_heap_break==32);
  active->p_heap_break=64;
  assert(cp32_fork_handoff(&active,&request)==0);
  assert(active==procs+4 && active->p_parent_pid==100 && active->p_heap_break==80);
  assert(cp32_fork_finish(&active,0,0)==200);
  assert(active==procs+3 && active->p_heap_break==64 && active->p_parent_pid==1);
  assert(procs[4].p_parent_pid==100 && procs[4].p_flags==P_ZOMBIE && !procs[4].p_heap_floor && !procs[4].p_heap_break);
  assert(cp32_fork_wait(200,0,0)==200);
  assert(!cp32_fork_start(&active,201));
  assert(active==procs+4 && active->p_heap_break==64 && active->p_parent_pid==100);
  assert(cp32_fork_finish(&active,0,0)==201 && cp32_fork_wait(201,0,0)==201);
  assert(!allocated && allocs==frees && refs==1);
 }
 puts("process state: persistent identity/heap, failed clone, exchange, cleared release and reuse PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(f.pre+f.proc+f.fs+f.bridge+f.source+setup+checks)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
app=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static unsigned char heap[8192];static unsigned used;static int mode,forks,waits,writes,execs;static jmp_buf done;
static void quit(int n){longjmp(done,n+1);}
static int pid(void){return 42;}
static int ppid(void){return 42;}
static void *grow(int n){assert((int)used+n>=0 && (int)used+n<8192);void *old=heap+used;used+=n;return old;}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n){assert(n==17 && !memcmp(p,"Process state OK\n",17));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 (void)n;
 if(op==CP32_APP_FORK){forks++;return mode==1 || mode==2 ? 0:100+forks;}
 if(op==CP32_APP_YIELD){if(mode==2)used++;return 0;}
 if(op==CP32_APP_EXEC){
  struct cp32_app_exec_request *q=p;execs++;
  if(!strcmp(q->path,"/missing"))return -2;
  assert(!strcmp(q->path,"/boot/hello") && q->argc==4 && used==2096 && execs==2);
  assert(!strcmp(q->args,"hello") && !strcmp(q->args+6,"--state-exec"));
  longjmp(done,300);
 }
 assert(op==CP32_APP_WAITPID);waits++;if(mode==5)return -10;*(int *)p=0;return fd;
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==4 ? 35:36,.read=input,.write=output,.exit=quit,.file=file,.getpid=pid,.getppid=ppid,.sbrk=grow};
 char old[24];snprintf(old,sizeof(old),"%lu",(unsigned long)(heap+2048));
 char *args[]={"hello",mode==3 ? "--state-exec":"--state","42",old,0};
 int exited=setjmp(done);
 if(!exited){int r=app_main(mode==3 ? 4:2,args,0,&s);
  if(mode==4)assert(r==90 && !forks);
  else if(mode==5)assert(r==249 && !writes);
  else if(mode==3)assert(!r && used==16 && !forks);
  else assert(!mode && !r && writes==1 && forks==2 && waits==2 && !used);
 }else if(mode==1)assert(exited==300 && execs==2);
 else assert(mode==2 && exited==245 && !execs);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'app.c').write_text(app)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(f.root/'src/apps/lib'),str(p/'app.c'),str(f.root/'tests/mm/state_hello.c'),str(f.root/'src/apps/lib/io.c'),'-o',str(p/'app')],check=True)
 for mode in range(6):subprocess.run([str(p/'app'),str(mode)],check=True)
print('hello state: independent growth, swap preservation, failed/successful exec and reuse PASS')
