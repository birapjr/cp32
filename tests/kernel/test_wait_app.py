"""Exercise hello --wait through app_main and the real ABI runtime.

Only the kernel service callbacks are modeled. This catches missing runtime
initialization, which a direct wait_test call with mocked wrappers hid in175.
"""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
test = r'''
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include "abi.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int calls, ids[8], codes[8], writes,tree_mode,exited,group=1;
static jmp_buf done;
static int file(unsigned op,int fd,void *buffer,unsigned arg){
 if(tree_mode){
  if(op==CP32_APP_GETPGRP)return group;
  if(op==CP32_APP_SETSID){if(group==99)return -1;group=99;return group;}
  if(op==CP32_APP_FORK){
   int call=calls++;
   if(call<tree_mode-1){group=1;return 0;}
   return tree_mode==3 ? -11:100+call;
  }
  if(op==CP32_APP_WAITPID && fd==0)return -10;
  assert(op==CP32_APP_WAITPID && fd==-(100+tree_mode-1));
  *(int *)buffer=9<<8;return -fd;
 }
 if(op==CP32_APP_FORK){
  assert(!fd && !buffer && !arg);
  int i;for(i=0;i<8 && ids[i];i++){}
  if(i==8)return -11;
  ids[i]=100+calls;codes[i]=(calls<8 ? calls+1:calls==8 ? 42:0)<<8;
  calls++;return ids[i];
 }
 assert(op==CP32_APP_WAITPID && arg<=1);
 for(int i=0;i<8;i++)if(ids[i] && (fd==-1 || fd==ids[i])){
  int pid=ids[i];*(int *)buffer=codes[i];ids[i]=0;return pid;
 }
 return -10;
}
static int output(const char *p,unsigned n){
 if(tree_mode)assert(n==13 && !memcmp(p,"Fork tree OK\n",13));
 else assert(n==17 && !memcmp(p,"Wait children OK\n",17));
 writes++;return n;
}
static int getpid(void){return 99;}
static int input(char *p,unsigned n){(void)p;(void)n;assert(0);return -1;}
static void child_exit(int n){assert(tree_mode>1);exited=n;longjmp(done,1);}
int main(int argc,char **argv){
 struct cp32_app_services s={0};
 s.version=27;s.getpid=getpid;s.read=input;s.write=output;s.exit=child_exit;s.file=file;
 char *args[]={"hello","--wait",0};
 if(argc>1){
  tree_mode=argv[1][0]-'0';args[1]="--tree";
  if(!setjmp(done)){assert(!app_main(2,args,0,&s));assert(tree_mode==1 && writes==1);}
  else assert(exited==9 && !writes);
  assert(calls==tree_mode);return 0;
 }
 assert(!app_main(2,args,0,&s) && calls==10 && writes==1);
 int pending=0;for(int i=0;i<8;i++)pending+=!!ids[i];assert(pending==1);
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)
    (p/'test.c').write_text(test)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra',
        '-Wno-misleading-indentation','-Wno-int-to-pointer-cast',
        '-Wno-pointer-to-int-cast','-fsanitize=undefined',
        '-I'+str(root/'src/apps/hello'),str(p/'test.c'),
        str(root/'src/apps/hello/hello.c'),str(root/'src/apps/lib/io.c'),
        str(root/'src/apps/lib/heap.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
    for mode in ("1","2","3"):
        subprocess.run([str(p/'test'),mode],check=True)
print('hello --wait: actual command entry, runtime initialization and service routing PASS')
