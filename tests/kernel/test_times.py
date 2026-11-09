"""Production accounting snapshot, ABI and actual hello command entry."""
from pathlib import Path
import sys, subprocess, tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
source=extract_function(root/'src/kernel/application.c','application_times')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "abi.h"
#define CP32_IRAM_EXT
static unsigned char memory[128];
#define CP32_APP_DATA ((uintptr_t)memory)
#define CP32_APP_TOP ((uintptr_t)(memory+sizeof(memory)))
struct proc {uint64_t user_time,sys_time,child_utime,child_stime;};
typedef struct {int m1_i2,m1_i3;char *m1_p1;} message;
static int locked,reads;
static int lock_save(void){int old=locked;locked=1;return old;}
static void restore_lock(int old){assert(locked);locked=old;}
static unsigned get_uptime(void){assert(locked);reads++;return 0xfffffff0U;}
'''
checks=r'''
int main(void){
 struct proc caller={0x100000002ULL,3,4,5};
 struct cp32_app_times t;
 message m={0,sizeof(t),(char *)memory+1};
 memset(memory,0x55,sizeof(memory));
 assert(!application_times(&caller,&m) && !locked && reads==1);
 memcpy(&t,m.m1_p1,sizeof(t));
 assert(t.user==2 && t.system==3 && t.children_user==4 && t.children_system==5 && t.uptime==0xfffffff0U);
 assert(memory[0]==0x55 && memory[1+sizeof(t)]==0x55);
 locked=1;m.m1_p1=(char *)CP32_APP_TOP-sizeof(t);
 assert(!application_times(&caller,&m) && locked);locked=0;
 unsigned char copy[128];memcpy(copy,memory,sizeof(copy));
 m.m1_p1++;assert(application_times(&caller,&m)==-14);
 m.m1_p1=0;assert(application_times(&caller,&m)==-14);
 m.m1_p1=(char *)(CP32_APP_DATA-1);assert(application_times(&caller,&m)==-14);
 m.m1_p1=(char *)(uintptr_t)-1;assert(application_times(&caller,&m)==-14);
 m.m1_p1=(char *)memory;m.m1_i3--;assert(application_times(&caller,&m)==-22);
 m.m1_i3=sizeof(t);m.m1_i2=1;assert(application_times(&caller,&m)==-22);
 assert(!memcmp(copy,memory,sizeof(copy)) && reads==2 && !locked);
}
'''
app=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,calls,writes,forks,waits,exited;static jmp_buf done;
static int rd(char *p,unsigned n){(void)p;(void)n;return 0;}
static int wr(const char *p,unsigned n){assert(n==17 && !memcmp(p,"Times OK (60 Hz)\n",17));writes++;return n;}
static void quit(int status){exited=status;longjmp(done,1);}
static int file(unsigned op,int fd,void *p,unsigned n){
 if(op==CP32_APP_FORK){forks++;return mode ? 0:123;}
 if(op==CP32_APP_WAITPID){assert(fd==123 && n==0);*(int *)p=0;waits++;return 123;}
 assert(op==CP32_APP_TIMES && !fd && p && n==sizeof(struct cp32_app_times));
 struct cp32_app_times *t=p;memset(t,0,sizeof(*t));calls++;
 if(!mode){t->children_user=calls>1 ? 2:0;t->children_system=calls>1 ? 7:0;}
 else if(calls>2){t->user=mode==2 ? 0:2;t->uptime=mode==2 ? 181:2;}
 return 0;
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=28,.read=rd,.write=wr,.exit=quit,.file=file};
 char *args[]={"hello","--time",0};
 if(!setjmp(done)){assert(!app_main(2,args,0,&s));assert(!mode && writes==1 && waits==1);}
 else assert(mode && exited==(mode==2 ? 125:0) && !writes && !waits);
 assert(forks==1 && calls==3);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)
 for name,code,extra in [('snapshot',pre+source+checks,[]),('app',app,[str(root/'src/apps/hello/hello.c'),str(root/'src/apps/lib/io.c')])]:
  (p/(name+'.c')).write_text(code)
  subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(root/'src/apps/hello'),'-I'+str(root/'src/apps/lib'),str(p/(name+'.c')),*extra,'-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
  if name=='app':
   for mode in ['1','2']:subprocess.run([str(p/name),mode],check=True)
print('times: atomic caller snapshot, ranges, wrap, ABI command parent/child/timeout PASS')
