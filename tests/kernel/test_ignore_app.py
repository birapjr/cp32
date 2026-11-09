"""Real ABI32 hello/runtime: ignore, pending discard, fork and exec routing."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,child,ignore,peer,writes,executed;static unsigned mask,pending;static jmp_buf done;
static int pid(void){return child ? 101:100;}
static void quit(int n){(void)n;assert(0);}
static int input(char *p,unsigned n){(void)p;(void)n;assert(0);return -1;}
static int output(const char *p,unsigned n){assert(n==17 && !memcmp(p,"Signal ignore OK\n",17));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 switch(op){
 case CP32_APP_SIGMASK:{unsigned old=mask;mask=n;return old;}
 case CP32_APP_SIGPENDING:return pending;
 case CP32_APP_SIGNAL:{int old=ignore;assert(fd==15 && n<=1);ignore=n;if(ignore)pending=0;return old;}
 case CP32_APP_KILL:assert(n==15);if(fd==pid()){if(!ignore){assert(mask);pending=mask;}}else assert(fd==101 && peer);return 0;
 case CP32_APP_FORK:assert(ignore);peer=ignore;if(mode==1){child=1;pending=0;return 0;}return 101;
 case CP32_APP_YIELD:assert(child && ignore);return 0;
 case CP32_APP_WAITPID:assert(!child && fd==101);if(n)return 0;*(int *)p=0;return 101;
 case CP32_APP_EXEC:{struct cp32_app_exec_request *r=p;assert(mode==1 && ignore && !strcmp(r->path,"/boot/hello"));executed=1;longjmp(done,1);}
 default:assert(0);return -1;
 }
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;ignore=mode==2;
 struct cp32_app_services s={.version=mode==3 ? 31:32,.read=input,.write=output,.exit=quit,.file=file,.getpid=pid};
 char *args[]={"hello",mode==2 ? "--igexec":"--ignore",0};
 if(!setjmp(done)){int r=app_main(2,args,0,&s);if(mode==3)assert(r==90);else if(mode==2)assert(!r && ignore && !writes);else assert(!r && !mode && writes==1 && !ignore && !pending);}
 else assert(mode==1 && executed && ignore);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'src/apps/hello/hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for n in range(4):subprocess.run([str(p/'test'),str(n)],check=True)
print('ignore app: pending discard, parent/child, exec entry and ABI gate PASS')
