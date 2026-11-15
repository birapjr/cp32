"""Run the actual hello --depth entry and runtime against deterministic replies."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,forks,waits,yields,writes,child;static jmp_buf done;
static int pid(void){return 100+child;}
static void quit(int n){longjmp(done,n+1);}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n){assert(n==14 && !memcmp(p,"Fork depth OK\n",14));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 switch(op){
 case CP32_APP_FORK:
  forks++;
  if(mode==1 || mode==4 || mode==6){if(forks<=(mode==4 ? 2:5)){child++;return 0;}return mode==6 ? -24:-12;}
  if(mode==2 && forks<=3){child++;return 0;}
  return 104;
 case CP32_APP_YIELD:assert(mode==1 && child==5);yields++;return 0;
 case CP32_APP_WAITPID:
  assert(fd==(n ? 104:-1));waits++;
  if(mode==2 && n){assert(*(int *)p==-1);return 0;}
  *(int *)p=(mode==5 && waits==2 ? 3:5)<<8;return 104;
 default:assert(0);return -1;
 }
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==3 ? 34:35,.read=input,.write=output,.exit=quit,.file=file,.getpid=pid};
 char *args[]={"hello","--depth",0};int exited=setjmp(done);
 if(!exited){int r=app_main(2,args,0,&s);
  if(mode==3)assert(r==90 && !forks);
  else if(mode==5)assert(r==228 && !writes);
  else assert(!mode && !r && writes==1 && forks==2 && waits==2);
 }else if(mode==1)assert(exited==6 && yields==1 && forks==6);
 else if(mode==2)assert(exited==6 && waits==2 && forks==4);
 else assert((mode==4 || mode==6) && exited==223 && !yields);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'tests/mm/process_hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for n in range(7):subprocess.run([str(p/'test'),str(n)],check=True)
print('depth app: repeated depth, live wait, deepest yield, exhaustion and ABI validation PASS')
