"""Process-owned backing images: commit, stable ownership, rollback and final release."""
from pathlib import Path
import subprocess,tempfile,sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
import test_fork as f
setup=f.test[:f.test.index('int main(void)')]
checks=r'''
int main(void){
 for(int cycle=0;cycle<20;cycle++) {
  setup();struct proc *active=procs+3;message request={2007,0,45};
  alloc_fail=1;assert(cp32_fork_start(&active,200)==-12 && !active->p_shadow);alloc_fail=0;
  fail_dup=1;assert(cp32_fork_start(&active,200)==-24 && !active->p_shadow);fail_dup=0;
  assert(!cp32_fork_start(&active,200));
  active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
  assert(procs[3].p_shadow==122 && !active->p_shadow && allocated==1);
  /* Generic activation has no FS/continuation side effects or allocations. */
  struct cp32_fproc *selected=cp32_active_files;
  struct cp32_fork_state *context=active_state;
  message continuation=context->resume;int allocations=allocs,releases=frees;
  live[0]=0x55;
  int irq=lock_save();
  window_busy=1;assert(cp32_image_activate(procs+3)==-16 && image_owner==active);window_busy=0;
  assert(cp32_image_activate(procs+6)==-22 && image_owner==active);
  assert(cp32_image_activate(0)==-22 && image_owner==active);
  assert(!cp32_image_activate(procs+3));
  assert(live[0]==original[0] && stage(123)[0]==0x55 && image_owner==procs+3);
  assert(cp32_active_files==selected && active_state==context && !memcmp(&continuation,&context->resume,sizeof(continuation)));
  assert(allocs==allocations && frees==releases);
  assert(!cp32_image_activate(procs+3)); /* resident selection is a no-op */
  assert(!cp32_image_activate(active) && live[0]==0x55 && image_owner==active);
  assert(cp32_active_files==selected && active_state==context && allocs==allocations && frees==releases);
  restore_lock(irq);
  struct proc saved=procs[3];
  assert(cp32_proc_release(procs+3)==EINVAL && !memcmp(&saved,procs+3,sizeof(saved)));
  assert(cp32_proc_clone(3,5,201,FS_PROC_NR)==EINVAL);
  assert(!cp32_fork_start(&active,201));
  active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
  assert(procs[3].p_shadow==122 && procs[4].p_shadow==123 && !active->p_shadow);
  for(int i=0;i<3;i++) {
   assert(cp32_fork_handoff(&active,&request)==(i ? 0:201));
   assert(active==procs+4 && !active->p_shadow && procs[5].p_shadow==124 && procs[3].p_shadow==122);
   assert(cp32_fork_handoff(&active,&request)==0);
   assert(active==procs+5 && !active->p_shadow && procs[4].p_shadow==123);
  }
  assert(cp32_fork_finish(&active,0,0)==201 && !active->p_shadow && !inuse[1]);
  assert(cp32_fork_wait(201,0,0)==201 && procs[3].p_shadow==122);
  assert(cp32_fork_handoff(&active,&request)==200);
  assert(active==procs+3 && !active->p_shadow && procs[4].p_shadow==123);
  assert(cp32_fork_handoff(&active,&request)==0);
  assert(active==procs+4 && !active->p_shadow && procs[3].p_shadow==122);
  assert(cp32_fork_finish(&active,0,0)==200 && !active->p_shadow);
  assert(cp32_fork_wait(200,0,0)==200 && !allocated && allocs==frees && refs==1);
  for(int n=3;n<NR_PROCS;n++)assert(!procs[n].p_shadow);
  assert(root_inuse);cp32_fork_reset();assert(!root_inuse && !image_owner);
 }
 puts("image owner: allocation rollback, parked release/clone rejection, nested exchanges and cleanup PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(f.pre+f.proc+f.fs+f.bridge+f.source+setup+checks)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
app=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static unsigned char heap[128];static unsigned used;static int mode,forks,yields,waits,writes;static jmp_buf done;
static void quit(int n){longjmp(done,n+1);}
static int pid(void){return 100;}
static void *grow(int n){assert((int)used+n>=0 && (int)used+n<128);void *p=heap+used;used+=n;return p;}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n){assert(n==19 && !memcmp(p,"Image ownership OK\n",19));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 (void)n;
 if(op==CP32_APP_FORK){forks++;return mode==1 ? 0:mode==3 ? -12:100+forks;}
 if(op==CP32_APP_YIELD){yields++;if(mode==2)heap[0]^=1;return 0;}
 if(op==CP32_APP_EXEC){struct cp32_app_exec_request *q=p;assert(!strcmp(q->path,"/boot/hello") && q->argc==2 && forks==2 && yields==6);longjmp(done,300);}
 assert(op==CP32_APP_WAITPID);waits++;if(mode==5)return -10;*(int *)p=0;return fd;
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==4 ? 35:36,.read=input,.write=output,.exit=quit,.file=file,.getpid=pid,.sbrk=grow};
 char *args[]={"hello",mode==6 ? "--image-exec":"--image",0};int exited=setjmp(done);
 if(!exited){int r=app_main(2,args,0,&s);
  if(mode==4)assert(r==90 && !forks);
  else if(mode==3)assert(r==234 && !writes);
  else if(mode==2)assert(r==235 && !writes);
  else if(mode==5)assert(r==236 && !writes);
  else if(mode==6)assert(!r && !forks);
  else assert(!mode && !r && writes==1 && forks==2 && yields==6 && waits==2 && !used);
 }else assert(mode==1 && exited==300);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'app.c').write_text(app)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(f.root/'src/apps/lib'),str(p/'app.c'),str(f.root/'tests/mm/image_hello.c'),str(f.root/'src/apps/lib/io.c'),'-o',str(p/'app')],check=True)
 for mode in range(7):subprocess.run([str(p/'app'),str(mode)],check=True)
print('hello image: nested child/exec, repeated parent exchange, reuse and failure reporting PASS')
