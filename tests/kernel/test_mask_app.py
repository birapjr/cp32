"""Real ABI31 hello mask command branches with modeled service replies."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,forks,child,writes;static unsigned mask,pending;static jmp_buf done;
static int pid(void){return child ? 200+forks:100;}
static void quit(int n){(void)n;assert(0);}
static int input(char *p,unsigned n){(void)p;(void)n;assert(0);return -1;}
static int output(const char *p,unsigned n){assert(n==15 && !memcmp(p,"Signal mask OK\n",15));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 switch(op){
 case CP32_APP_SIGMASK:{unsigned old=mask;
  assert(!p);if(fd==4)return old;
  if(fd==0)mask|=n&CP32_SIGTERM_MASK;else if(fd==1)mask&=~n;else {assert(fd==2);mask=n&CP32_SIGTERM_MASK;}
  if(pending && !mask){assert(child);longjmp(done,15);}return old;}
 case CP32_APP_SIGPENDING:assert(!fd && !p && !n);return pending;
 case CP32_APP_FORK:forks++;
  if(mode==1 && forks==1){child=1;pending=0;return 0;}return 200+forks;
 case CP32_APP_YIELD:assert(child);pending=CP32_SIGTERM_MASK;return 0;
 case CP32_APP_KILL:assert(!child && fd==200+forks && (n==9 || n==15));return 0;
 case CP32_APP_WAITPID:
  assert(fd==200+forks);if(n)return 0;
  *(int *)p=child ? 0:forks==2 ? 9:15;return fd;
 default:assert(0);return -1;
 }
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==2 ? 30:31,.read=input,.write=output,.exit=quit,.file=file,.getpid=pid};
 char *args[]={"hello","--mask",0};int result=setjmp(done);
 if(!result){int r=app_main(2,args,0,&s);if(mode==2)assert(r==90 && !forks);else assert(!r && !mode && forks==1 && writes==1 && !mask);}
 else assert(mode==1 && result==15 && forks==1);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'src/apps/hello/hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for n in range(3):subprocess.run([str(p/'test'),str(n)],check=True)
print('mask app: parent/child, pending termination, fork mask and ABI gate PASS')
