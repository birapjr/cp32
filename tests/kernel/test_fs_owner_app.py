"""Current hello FS ownership probe: parent, child and failure branches."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=r'''
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,opens,forks,waits,reads,closes,writes;static unsigned mask=0022;
static char cwd[16]="/";static jmp_buf done;
static void quit(int n){longjmp(done,n+1);}
static int output(const char *p,unsigned n){if(mode==2){if(!writes)assert(n==21 && !memcmp(p,"[FS V202 open errno] ",21));else assert(n==2 && !memcmp(p,"2\n",2));writes++;return n;}assert(n==16 && !memcmp(p,"FS ownership OK\n",16));writes++;return n;}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int file(unsigned op,int fd,void *p,unsigned n){
 switch(op) {
 case CP32_APP_CHDIR:strcpy(cwd,p);return 0;
 case CP32_APP_UMASK:{unsigned old=mask;mask=n;return old;}
 case CP32_APP_GETCWD:assert(n>=strlen(cwd)+1);strcpy(p,cwd);return 0;
 case CP32_APP_OPEN:assert(!strcmp(p,"/boot/hello"));opens++;return mode==2 ? -2:3;
 case CP32_APP_FORK:forks++;return mode==1 || mode==5 ? 0:100+forks;
 case CP32_APP_YIELD:return mode==5 ? -5:0;
 case CP32_APP_SEEK_CUR:assert(fd==3);return mode==3 ? 0:1;
 case CP32_APP_WAITPID:waits++;*(int *)p=mode==4 ? 7<<8:0;return fd;
 case CP32_APP_READ:assert(fd==3 && n==1);*(char *)p='a';reads++;return 1;
 case CP32_APP_CLOSE:assert(fd==3);closes++;return 0;
 default:assert(0);return -1;
 }
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=mode==6 ? 35:36,.write=output,.exit=quit,.read=input,.file=file};
 char *args[]={"hello","--fs-owner",0};int exited=setjmp(done);
 if(!exited){int r=app_main(2,args,0,&s);
  if(mode==2)assert(r==232 && !forks && writes==2);
  else if(mode==3)assert(r==237 && !waits);
  else if(mode==4)assert(r==238);
  else if(mode==6)assert(r==90 && !opens);
  else assert(!mode && !r && opens==2 && forks==2 && waits==2 && reads==2 && closes==2 && writes==1);
 }else if(mode==1)assert(exited==1 && reads==1 && closes==1 && !writes);
 else assert(mode==5 && exited==236 && !closes);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'tests/mm/boundary_hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for mode in range(7):subprocess.run([str(p/'test'),str(mode)],check=True)
print('FS owner app: parent namespace/shared offset, child close, reuse and failure reporting PASS')
