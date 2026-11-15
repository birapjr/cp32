"""Exercise the shipped group test, including stale ABI and failure detection."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,rounds,forks,writes,group_kills;static jmp_buf done;
static int pid(void){return mode==2 ? 102:mode==1 ? 101:100;}
static void quit(int n){longjmp(done,n+1);}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n){assert(n==17 && !memcmp(p,"Group signals OK\n",17));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 switch(op){
 case CP32_APP_KILL:
  if(fd==(-2147483647-1))return -22;
  if(fd==-9999)return mode==4 ? 0:-3;
  assert(mode==2 && fd==0 && (n==0 || n==15));group_kills++;return 0;
 case CP32_APP_SIGNAL:assert(fd==12 || (mode==2 && fd==15));return 0;
 case CP32_APP_FORK:forks++;return mode==2 || (mode==1 && forks==1) ? 0:101;
 case CP32_APP_SETSID:return pid();
 case CP32_APP_WAITPID:assert(!mode && fd==101);*(int *)p=rounds==1 ? 10:rounds==3 ? 12:15;rounds++;return fd;
 default:assert(0);return -1;
 }
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==3 ? 33:34,.read=input,.write=output,.exit=quit,.file=file,.getpid=pid};
 char *args[]={"hello","--groups",0};int exited=setjmp(done);
 if(!exited){int r=app_main(2,args,0,&s);
  if(mode==3)assert(r==90 && !forks);
  else if(mode==4)assert(r==210 && !forks);
  else assert(!mode && !r && writes==1 && rounds==4 && forks==4);
 } else if(mode==1)assert(exited==219 && forks==2); /* resumed killed parent is detected */
 else assert(mode==2 && exited==1 && forks==2 && group_kills==2);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'tests/mm/process_hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for n in range(5):subprocess.run([str(p/'test'),str(n)],check=True)
print('group signals app: four wait results, grandchild delivery, failure detection and ABI gate PASS')
