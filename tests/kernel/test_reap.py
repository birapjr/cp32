"""Logical process reservations include exited children in every saved image."""
from pathlib import Path
import subprocess,tempfile,sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
import test_fork as f
pre=f.pre.replace('#define NR_PROCS 12','#define NR_PROCS 9')
setup=f.test[:f.test.index('int main(void)')]
checks=r'''
int main(void){
 for(int round=0;round<10;round++) {
  setup();cp32_fork_bind(procs+3);
  struct proc *active=procs+3;int pp=1;uint32_t floor=16,brk=32;
  for(int n=0;n<3;n++) {
   assert(!cp32_fork_start(&active,200+n,&pp,&floor,&brk));
   assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,0)==200+n);
  }
  assert(!cp32_fork_start(&active,300,&pp,&floor,&brk));
  active->p_flags=RECEIVING;active->p_getfrom=FS_PROC_NR;
  assert(!cp32_fork_start(&active,301,&pp,&floor,&brk));
  assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,0)==301);
  int before=allocs;
  assert(cp32_fork_start(&active,302,&pp,&floor,&brk)==-11 && allocs==before && refs==2);
  assert(cp32_fork_wait(301,(void *)(CP32_APP_TOP-1),0)==-14);
  assert(!fork_capacity());
  message request={2007,0,45};
  assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==300);
  assert(!fork_capacity()); /* parked child's retained exit also reserves capacity */
  assert(cp32_fork_handoff(&active,&pp,&floor,&brk,&request)==0);
  assert(cp32_fork_wait(301,0,0)==301 && fork_capacity());
  assert(!cp32_fork_start(&active,302,&pp,&floor,&brk));
  assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,0)==302);
  assert(!fork_capacity());
  /* Parent exit discards its uncollected child and releases that reservation. */
  assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,0)==300);
  assert(fork_capacity() && !cp32_fork_pid_used(302));
  assert(!cp32_fork_start(&active,400,&pp,&floor,&brk));
  assert(cp32_fork_finish(&active,0,&pp,&floor,&brk,0)==400);
  assert(!fork_capacity());
  for(int n=0;n<5;n++)assert(cp32_fork_wait(-1,0,0)>0);
  assert(cp32_fork_wait(-1,0,0)==-10 && fork_capacity());
  assert(!allocated && allocs==frees && refs==1);
 }
 puts("reap: ancestor/parked reservations, EAGAIN before allocation, EFAULT retention, wait/exit reuse PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+f.proc+f.fs+f.bridge+f.source+f.compat+setup+f.historic(checks))
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
app=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,forks,waits,writes;static jmp_buf done;
static void quit(int n){longjmp(done,n+1);}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n){assert(n==17 && !memcmp(p,"Process slots OK\n",17));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 (void)n;
 if(op==CP32_APP_FORK){
  forks++;
  if(mode==1 || mode==3){
   if(forks<=24)return forks%8 ? 100+forks:0;
   if(forks==29)return mode==3 ? -12:-11;
  }
  return 100+forks;
 }
 assert(op==CP32_APP_WAITPID);waits++;
 if(mode==4)return -10;
 *(int *)p=0;return fd==-1 ? 100:fd;
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==2 ? 35:36,.read=input,.write=output,.exit=quit,.file=file};
 char *args[]={"hello","--slots",0};int exited=setjmp(done);
 if(!exited){int r=app_main(2,args,0,&s);
  if(mode==2)assert(r==90 && !forks);
  else if(mode==4)assert(r==238 && !writes);
  else assert(!mode && !r && writes==1 && forks==8 && waits==8);
 }else if(mode==1)assert(exited==1 && forks==30 && waits==5 && !writes);
 else assert(mode==3 && exited==232 && !writes);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'app.c').write_text(app)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(f.root/'src/apps/lib'),str(p/'app.c'),str(f.root/'tests/mm/reap_hello.c'),str(f.root/'src/apps/lib/io.c'),'-o',str(p/'app')],check=True)
 for mode in range(5):subprocess.run([str(p/'app'),str(mode)],check=True)
print('current hello: root/leaf capacity, reuse, ABI, ENOMEM and wait failures PASS')
