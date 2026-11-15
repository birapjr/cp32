"""Real exited slots: >8 children, capacity, ownership, copyout and reuse."""
from pathlib import Path
import subprocess,tempfile,sys
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).parent))
import test_fork as f
pre=f.pre.replace('#define NR_PROCS 12','#define NR_PROCS 20')
setup=f.test[:f.test.index('int main(void)')]
checks=r'''
int main(void){
 for(int cycle=0;cycle<10;cycle++) {
  setup();struct proc *active=procs+3;int status;
  for(int i=0;i<16;i++) {
   assert(!cp32_fork_start(&active,200+i));
   assert(cp32_fork_wait(200,0,0)==-10); /* no sibling collection */
   active->user_time=1;
   assert(cp32_fork_finish(&active,i,0)==200+i);
   struct proc *z=procs+4+i;
   assert(z->p_flags==P_ZOMBIE && z->p_pid==200+i && z->p_parent_pid==100);
   assert(z->p_exitstatus==(i<<8) && !z->p_shadow && !z->p_messbuf && !z->p_reg.pc);
   assert(!allocated && refs==1 && active->child_utime==(unsigned)i+1);
  }
  int before=allocs;assert(cp32_fork_start(&active,300)==-11 && allocs==before);
  assert(cp32_fork_pid_used(205) && !cp32_fork_signal_target(active,205,0));
  assert(cp32_fork_wait(205,(void *)(CP32_APP_TOP-1),0)==-14);
  assert(procs[9].p_flags==P_ZOMBIE && !fork_capacity());
  assert(cp32_fork_wait(205,(void *)CP32_APP_DATA,0)==205);
  memcpy(&status,live+16384,4);assert(status==(5<<8));
  assert(procs[9].p_flags==P_SLOT_FREE && !cp32_fork_pid_used(205));
  assert(!cp32_fork_start(&active,300) && active==procs+9);
  assert(cp32_fork_finish(&active,0,15)==300);
  assert(cp32_fork_wait(300,(void *)CP32_APP_DATA,0)==300);
  memcpy(&status,live+16384,4);assert(status==15);
  for(int i=15;i>=0;i--)if(i!=5)assert(cp32_fork_wait(200+i,0,0)==200+i);
  assert(cp32_fork_wait(-1,0,0)==-10 && active->child_utime==16);
  assert(!allocated && refs==1 && allocs==frees);
  for(int n=4;n<NR_PROCS;n++)assert(procs[n].p_flags==P_SLOT_FREE && !procs[n].p_exitstatus);
 }
 puts("zombies: 16 retained exits, real slot exhaustion, EFAULT retention, status and reuse PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+f.proc+f.fs+f.bridge+f.source+setup+checks)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
app=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,forks,writes,alive[20],statuses[20];static jmp_buf done;
static void quit(int n){longjmp(done,n+1);}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n){assert(n==15 && !memcmp(p,"Zombie wait OK\n",15));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 (void)n;int limit=mode==2 ? 8:10;
 if(op==CP32_APP_FORK){forks++;if(mode==1)return 0;if(forks==limit+1)return -11;int i=forks-1;alive[i]=1;statuses[i]=forks>limit ? (forks==limit+2 ? 77:55):i;return 100+i;}
 if(op==CP32_APP_KILL)return alive[fd-100] ? 0:-3;
 assert(op==CP32_APP_WAITPID);
 if(fd==-1)return -10;
 int i=fd-100;assert(alive[i]);
 if(p==(void *)1)return mode==3 ? fd:-14;
 *(int *)p=(mode==5 ? 99:statuses[i])<<8;alive[i]=0;return fd;
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==4 ? 35:36,.read=input,.write=output,.exit=quit,.file=file};
 char *args[]={"hello","--zombie",0};int exited=setjmp(done);
 if(!exited){int r=app_main(2,args,0,&s);
  if(mode==4)assert(r==90 && !forks);
  else if(mode==2)assert(r==232 && !writes);
  else if(mode==3)assert(r==233 && !writes);
  else if(mode==5)assert(r==234 && !writes);
  else assert(!mode && !r && writes==1 && forks==13);
 }else assert(mode==1 && exited==1);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'app.c').write_text(app)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(f.root/'src/apps/lib'),str(p/'app.c'),str(f.root/'src/apps/hello/hello.c'),str(f.root/'src/apps/lib/io.c'),'-o',str(p/'app')],check=True)
 for mode in range(6):subprocess.run([str(p/'app'),str(mode)],check=True)
print('hello zombie: capacity, selective waits, reuse, child exit and failure reporting PASS')
