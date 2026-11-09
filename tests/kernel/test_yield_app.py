"""Real ABI29 hello dispatcher for cooperative parent/child acceptance."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,child,forks,yields,waits,writes,used,status=-1,executed;
static char cwd[16]="/";static unsigned char heap[64];static jmp_buf done;
static int getpid(void){return mode==6 && forks>1 ? 98:child ? 99:100;}
static int getppid(void){return mode==6 ? 99:100;}
static void *brk(int n){assert(used+n>=0 && used+n<=64);void *p=heap+used;used+=n;return p;}
static void quit(int n){assert(child);status=n;longjmp(done,1);}
static int input(char *p,unsigned n){(void)p;(void)n;assert(0);return -1;}
static int output(const char *p,unsigned n){assert(n==14 && !memcmp(p,mode==4 ? "Yield tree OK\n":mode==2 ? "Yield exit OK\n":"Yield pair OK\n",14));writes++;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){
 switch(op){
 case CP32_APP_CHDIR:strcpy(cwd,p);return 0;
 case CP32_APP_GETCWD:assert(n>=strlen(cwd)+1);strcpy(p,cwd);return 0;
 case CP32_APP_UMASK:return 0022;
 case CP32_APP_OPEN:return 3;
 case CP32_APP_CLOSE:assert(fd==3);return 0;
 case CP32_APP_READ:assert(child && fd==3 && n==1);*(char *)p='a';return 1;
 case CP32_APP_SETSID:case CP32_APP_GETPGRP:assert(child);return getpid();
 case CP32_APP_FORK:forks++;if(mode>=5){if(forks==1){child=1;return 0;}if(forks==2)return mode==6 ? 0:101;return -11;}if(forks>1)return -11;if(mode==1){child=1;return 0;}return 101;
 case CP32_APP_YIELD:yields++;return 0;
 case CP32_APP_SEEK_CUR:assert((!child || mode==5) && yields==1);return 2;
 case CP32_APP_WAITPID:assert((!child || mode==5) && fd==101);waits++;if(n==1)return 0;*(int *)p=mode==4 ? 0:7<<8;return 101;
 case CP32_APP_EXEC:assert(mode==3 && !strcmp(((struct cp32_app_exec_request *)p)->path,"/boot/echo"));executed=1;longjmp(done,1);
 default:assert(0);return -1;
 }
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=29,.read=input,.write=output,.exit=quit,.file=file,.getpid=getpid,.getppid=getppid,.sbrk=brk};
 char *args[]={"hello",mode>=4 ? "--ytree":mode==2 ? "--yexit":mode==3 ? "--yexec":"--yield",0};
 if(!setjmp(done)){
  assert(!app_main(2,args,0,&s));
  if(mode==4)assert(writes==1 && waits==2 && forks==1 && !yields);
  else if(mode==2)assert(writes==1 && waits==1 && !yields);
  else assert(!mode && writes==1 && forks==2 && waits==2 && yields==1 && !used);
 }else if(mode==5)assert(status==0 && yields==2 && waits==2 && writes==1 && forks==3 && !used);
 else if(mode==6)assert(status==7 && yields==3 && !waits && !writes && forks==2);
 else if(mode==1)assert(status==7 && yields==3 && !waits && !writes);
 else assert(mode==3 && executed && waits==1 && !writes);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'src/apps/hello/hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for n in range(7):subprocess.run([str(p/'test'),str(n)],check=True)
print('yield app: real runtime/commands, parent, child, early exit and exec routing PASS')
