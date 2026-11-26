"""Current device fault-ownership test with deterministic runtime replies."""
from pathlib import Path
import subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
hello=(root/'tests/mm/fault_owner_hello.c').read_text()
trigger=extract_function(root/'tests/mm/fault_owner_hello.c','trigger_fault')
hello=hello.replace('../../src/apps/hello/abi.h',str(root/'src/apps/hello/abi.h')).replace('../../src/apps/lib/io.h',str(root/'src/apps/lib/io.h'))
hello=hello.replace(trigger,'extern void simulated_fault(int);\nstatic __attribute__((noreturn)) void trigger_fault(int kind){simulated_fault(kind);__builtin_unreachable();}')
app=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static unsigned char heap[128];static unsigned used;static int mode,forks,waits,writes;static jmp_buf done;
static void quit(int n){longjmp(done,n+1);}
void simulated_fault(int kind){assert((mode==1 && kind==0) || (mode==6 && kind==1));longjmp(done,100+kind);}
static void *grow(int n){if(mode==5 && forks==1 && n>0)return (void *)-1;assert((int)used+n>=0 && (int)used+n<128);void *p=heap+used;used+=n;return p;}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n){assert(n==19 && !memcmp(p,"Fault ownership OK\n",19));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 (void)n;
 if(op==CP32_APP_FORK){forks++;return (mode==1 && forks==1) || (mode==2 && forks==2) || (mode==6 && forks==3) ? 0:100+forks;}
 assert(op==CP32_APP_WAITPID);waits++;
 *(int *)p=mode==4 ? 0:forks%2 ? (forks==1 ? 4:11):(7<<8);return fd;
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==3 ? 35:36,.read=input,.write=output,.exit=quit,.file=file,.sbrk=grow};
 char *args[]={"hello","--fault-owner",0};int exited=setjmp(done);
 if(!exited){int r=app_main(2,args,0,&s);
  if(mode==3)assert(r==90 && !forks);
  else if(mode==4)assert(r==233 && !writes);
  else if(mode==5)assert(r==234 && !writes);
  else assert(!mode && !r && writes==1 && forks==4 && waits==4 && !used);
 }else if(mode==1)assert(exited==100);
 else if(mode==2)assert(exited==8);
 else assert(mode==6 && exited==101);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'app.c').write_text(app);(p/'hello.c').write_text(hello)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(root/'src/apps/hello'),'-I'+str(root/'src/apps/lib'),str(p/'app.c'),str(p/'hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'app')],check=True)
 for mode in range(7):subprocess.run([str(p/'app'),str(mode)],check=True)
print('fault ownership app: distinct faults, parent heap, normal reused child and failure reporting PASS')
