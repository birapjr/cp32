"""Persistent signal/group ownership, independent of shadow-image copies."""
from pathlib import Path
import subprocess,tempfile,sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
import test_fork as f
setup=f.test[:f.test.index('int main(void)')]
checks=r'''
int main(void){
 for(int cycle=0;cycle<20;cycle++) {
  setup();struct proc *active=procs+3;
  assert(!cp32_fork_sigmask(0,1U<<14));
  assert(!cp32_fork_disposition(10,1));
  assert(!cp32_fork_signal_target(active,100,15));
  assert(active->p_app_pending==(1U<<14));
  assert(!cp32_fork_start(&active,200));
  assert(active->p_app_group==1 && active->p_app_sigmask==(1U<<14) && !active->p_app_pending && active->p_app_ignore==(1U<<9));
  assert(procs[3].p_app_pending==(1U<<14));
  assert(cp32_process_group(200,1)==200);
  assert(!cp32_fork_signal_target(active,200,15));
  active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
  message request={2007,0,45};
  assert(cp32_fork_handoff(&active,&request)==200);
  assert(active==procs+3 && cp32_process_group(100,0)==1);
  assert(!cp32_fork_disposition(15,1));
  assert(!active->p_app_pending && procs[4].p_app_pending==(1U<<14));
  assert(cp32_fork_handoff(&active,&request)==0);
  assert(active==procs+4 && cp32_process_group(200,0)==200 && cp32_fork_sigpending()==(1U<<14));
  assert(cp32_fork_finish(&active,0,0)==200);
  assert(procs[4].p_app_group==200 && procs[4].p_flags==P_ZOMBIE && !procs[4].p_app_sigmask && !procs[4].p_app_pending && !procs[4].p_app_ignore);
  assert(cp32_fork_wait(-200,0,0)==200); /* exit retains the child's group */
  assert(!cp32_fork_start(&active,201));
  assert(active->p_app_group==1 && !active->p_app_pending && active->p_app_ignore==((1U<<9)|(1U<<14)));
  assert(cp32_fork_finish(&active,0,0)==201 && cp32_fork_wait(201,0,0)==201);
  cp32_fork_reset();assert(!active->p_app_sigmask && !active->p_app_pending && !active->p_app_ignore);
  assert(!allocated && allocs==frees && refs==1);
 }
 puts("signal owner: inheritance, distinct pending/group, swap, retained exit group and reuse PASS");
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
static int mode,forks,waits,writes,group=1;static unsigned mask,pending,ignore;static jmp_buf done;
static void quit(int n){longjmp(done,n+1);}
static int pid(void){return 100;}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n){assert(n==20 && !memcmp(p,"Signal ownership OK\n",20));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 if(op==CP32_APP_GETPGRP)return group;
 if(op==CP32_APP_SETSID)return group=100;
 if(op==CP32_APP_SIGMASK){unsigned old=mask;if(fd==4)return old;if(fd==2)mask=n;else assert(0);return old;}
 if(op==CP32_APP_SIGPENDING)return pending;
 if(op==CP32_APP_SIGNAL){unsigned bit=1U<<(fd-1),old=!!(ignore&bit);if(n){ignore|=bit;pending&=~bit;}else ignore&=~bit;return old;}
 if(op==CP32_APP_KILL){pending|=1U<<(n-1);return 0;}
 if(op==CP32_APP_FORK){forks++;if(mode==1 || mode==2){pending=0;return 0;}return 100+forks;}
 if(op==CP32_APP_YIELD){if(mode==2)pending=0;return 0;}
 if(op==CP32_APP_EXEC){struct cp32_app_exec_request *q=p;assert(!strcmp(q->path,"/boot/hello") && q->argc==2 && group==100 && mask==(1U<<14) && pending==mask);longjmp(done,300);}
 assert(op==CP32_APP_WAITPID);waits++;*(int *)p=0;return fd;
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==4 ? 35:36,.read=input,.write=output,.exit=quit,.file=file,.getpid=pid};
 char *args[]={"hello",mode==3 ? "--owner-exec":"--owner",0};
 if(mode==3){group=100;mask=pending=1U<<14;ignore=1U<<9;}
 int exited=setjmp(done);
 if(!exited){int r=app_main(2,args,0,&s);
  if(mode==4)assert(r==90 && !forks);
  else if(mode==3)assert(!r && !mask && !pending);
  else assert(!mode && !r && writes==1 && forks==2 && waits==2);
 }else if(mode==1)assert(exited==300);
 else assert(mode==2 && exited==239);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'app.c').write_text(app)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(f.root/'src/apps/lib'),str(p/'app.c'),str(f.root/'tests/mm/owner_hello.c'),str(f.root/'src/apps/lib/io.c'),'-o',str(p/'app')],check=True)
 for mode in range(5):subprocess.run([str(p/'app'),str(mode)],check=True)
print('hello ownership: parent/child/exec paths, stale ABI and lost pending failure PASS')
