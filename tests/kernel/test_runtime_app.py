"""Device I/O/exec acceptance code against deterministic service responses.

Actual concurrent scheduling, Xtensa context restoration and SD timing remain
hardware tests. These exercise the same app's success and failure decisions.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=r'''
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "io.h"
#include "hello.c"
static int mode,forked,reads,pre_reads,closed,execs,writes,position;
static jmp_buf finished;
static void quit(int n){longjmp(finished,n+1);}
static int getpid(void){return mode>=2 || (mode==1 && forked) ? 101:100;}
static int getppid(void){return 100;}
static int output(const char *text,unsigned count){
 assert(count==23 && !memcmp(text,"Concurrent I/O exec OK\n",23));writes++;return count;
}
static int input(char *p,unsigned n){(void)p;(void)n;assert(0);return -1;}
static int file(unsigned op,int fd,void *pointer,unsigned arg){
 switch(op){
 case CP32_APP_SIGNAL:
  if(mode>=2 && fd==CP32_SIGUSR2)return CP32_SIG_IGN;
  return CP32_SIG_DFL;
 case CP32_APP_CHDIR:return 0;
 case CP32_APP_OPEN:assert(!strcmp(pointer,"/boot/hello"));return mode==3 ? -2:3;
 case CP32_APP_PREAD:{
  struct cp32_app_position_request *q=pointer;assert(fd==3 && arg==sizeof(*q));
  for(unsigned i=0;i<q->count;i++)q->data[i]=(char)(q->offset+i);
  pre_reads++;return mode==4 && pre_reads==3 ? -5:(int)q->count;
 }
 case CP32_APP_READ:assert(fd==3 && arg==1);*(char *)pointer=(char)position++;reads++;return 1;
 case CP32_APP_FORK:forked=1;return mode==1 ? 0:101;
 case CP32_APP_SEEK_CUR:assert(fd==3 && !arg);return position;
 case CP32_APP_GETCWD:strcpy(pointer,mode==1 ? "/boot":"/");return 0;
 case CP32_APP_EXEC:{
  struct cp32_app_exec_request *q=pointer;assert(q->argc==5);execs++;
  static const char expected[]="hello\0--exec-child\0" "3\0" "101\0" "100\0";
  assert(!memcmp(q->args,expected,sizeof(expected)-1));
  if(execs==1){assert(!strcmp(q->path,"/boot/.cp32-noexec"));return -2;}
  assert(!strcmp(q->path,"/boot/hello"));longjmp(finished,1);
 }
 case CP32_APP_WAITPID:assert(fd==101);*(int *)pointer=0;received=1;position=3;return 101;
 case CP32_APP_KILL:assert(fd==100 && (arg==0 || arg==CP32_SIGUSR1));return 0;
 case CP32_APP_CLOSE:assert(fd==3);closed++;return 0;
 default:assert(0);return -1;
 }
}
int main(int argc,char **argv){
 mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=37,.write=output,.exit=quit,.getpid=getpid,
  .getppid=getppid,.read=input,.file=file};
 char *args[]={"hello","--io-exec",0};
 char *child[]={"hello","--exec-child","3","101","100",0};
 if(mode==2)position=2;
 int jumped=setjmp(finished);
 if(!jumped){
  int r=app_main(mode==2 ? 5:2,mode==2 ? child:args,0,&s);
  if(mode==3)assert(r==239 && !forked);
  else if(mode==4)assert(r==242 && forked && !closed);
  else if(mode==2)assert(!r && reads==1 && pre_reads==1 && closed==1 && !writes);
  else assert(!mode && !r && reads==1 && pre_reads==17 && closed==1 && writes==1);
 }else assert(mode==1 && jumped==1 && execs==2 && reads==2 && pre_reads==17 && !closed);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',
  '-I'+str(root/'src/apps/lib'),'-I'+str(root/'src/apps/hello'),str(p/'test.c'),
  str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for mode in range(5):subprocess.run([str(p/'test'),str(mode)],check=True)
print('runtime app: private I/O buffers, inherited exec descriptors/PID, signal dispositions and error reporting PASS')
