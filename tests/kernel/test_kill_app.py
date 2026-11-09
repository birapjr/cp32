"""Real hello/runtime kill acceptance: parent, parked child and self delivery."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,forks,child,exists,dead,sig,writes,closed;static jmp_buf done;
static int pid(void){return child ? 200+forks:100;}
static void quit(int n){(void)n;assert(0);}
static int input(char *p,unsigned n){(void)p;(void)n;assert(0);return -1;}
static int output(const char *p,unsigned n){assert(n==13 && !memcmp(p,"Kill wait OK\n",13));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 switch(op){
 case CP32_APP_OPEN:return 3;
 case CP32_APP_CLOSE:assert(fd==3);closed++;return 0;
 case CP32_APP_READ:assert(child && fd==3 && n==1);*(char *)p='a';return 1;
 case CP32_APP_SEEK_CUR:assert(fd==3);return forks;
 case CP32_APP_FORK:forks++;exists=1;dead=forks==3;sig=15;
  if(mode==1 || (mode==2 && forks==3)){child=1;return 0;}return 200+forks;
 case CP32_APP_YIELD:assert(mode==1 && child);longjmp(done,9);
 case CP32_APP_KILL:
  assert(!p);
  if(fd==0)return -22;
  if(fd==pid()) {if(!n)return 0;assert(mode==2 && child && n==15);longjmp(done,15);}
  assert(fd==200+forks);if(!exists)return -3;
  if(!n)return 0;assert(!dead && (n==9 || n==15));dead=1;sig=n;return 0;
 case CP32_APP_WAITPID:
  assert(!child && fd==200+forks && exists);
  if(n){assert(!dead);return 0;}
  assert(dead);*(int *)p=sig;exists=0;return fd;
 default:assert(0);return -1;
 }
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==3 ? 29:30,.read=input,.write=output,.exit=quit,.file=file,.getpid=pid};
 char *args[]={"hello","--kill",0};int signal=setjmp(done);
 if(!signal){int r=app_main(2,args,0,&s);if(mode==3)assert(r==90 && !forks);else assert(!r && !mode && forks==3 && writes==1 && closed==1 && !exists);}
 else assert((mode==1 && signal==9 && forks==1) || (mode==2 && signal==15 && forks==3));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'src/apps/hello/hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for n in range(4):subprocess.run([str(p/'test'),str(n)],check=True)
print('kill app: real runtime, parent delivery/wait, child suspension, self termination and ABI gate PASS')
