from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,child,writes;static unsigned mask,pending,ignored;static jmp_buf done;
static int pid(void){return child ? 101:100;}
static void quit(int n){(void)n;assert(0);}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int output(const char *p,unsigned n){assert(n==11 && !memcmp(p,"Signals OK\n",11));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 switch(op){
 case CP32_APP_SIGMASK:{unsigned old=mask;if(fd==0)mask|=n;else if(fd==1)mask&=~n;else mask=n;if(pending&~mask)longjmp(done,10);return old;}
 case CP32_APP_SIGPENDING:return pending;
 case CP32_APP_SIGNAL:{unsigned bit=CP32_SIGSET(fd),old=!!(ignored&bit);if(n){ignored|=bit;pending&=~bit;}else ignored&=~bit;return old;}
 case CP32_APP_FORK:if(mode==1){child=1;return 0;}return 101;
 case CP32_APP_YIELD:assert(child);pending=CP32_SIGSET(10)|CP32_SIGSET(12);return 0;
 case CP32_APP_KILL:if(fd==100)assert(n==1 && (ignored&1));else assert(fd==101 && (n==10 || n==12));return 0;
 case CP32_APP_WAITPID:assert(fd==101);*(int *)p=10;return 101;
 default:assert(0);return -1;
 }
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==2 ? 32:33,.read=input,.write=output,.exit=quit,.file=file,.getpid=pid};
 char *args[]={"hello","--signals",0};int signal=setjmp(done);
 if(!signal){int r=app_main(2,args,0,&s);if(mode==2)assert(r==90);else assert(!mode && !r && writes==1 && !ignored && mask==(CP32_SIGSET(10)|CP32_SIGSET(12)));}
 else assert(mode==1 && signal==10 && pending==CP32_SIGSET(10));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'src/apps/hello/hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for n in range(3):subprocess.run([str(p/'test'),str(n)],check=True)
print('signals app: independent masks, pending-bit removal, wait status and ABI gate PASS')
