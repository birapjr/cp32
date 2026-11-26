"""Actual device CPU/stack workload, uptime boundary and callback failure."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=r'''
#include <assert.h>
#include <string.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static int mode,calls,writes;
static void quit(int n){(void)n;assert(0);}
static int output(const char *p,unsigned n){assert(n==13 && !memcmp(p,"IRQ stack OK\n",13));writes++;return n;}
static int input(char *p,unsigned n){(void)p;(void)n;return 0;}
static int file(unsigned op,int fd,void *p,unsigned n){
 assert(op==CP32_APP_TIMES && !fd && n==sizeof(struct cp32_app_times));calls++;
 if(mode==1 || (mode==2 && calls==2))return -5;
 struct cp32_app_times *t=p;memset(t,0,sizeof(*t));
 t->uptime=0xfffffff8U+4U*(unsigned)calls;return 0;
}
int main(int argc,char **argv){mode=argc>1 ? argv[1][0]-'0':0;
 struct cp32_app_services s={.version=36,.write=output,.exit=quit,.read=input,.file=file};
 char *args[]={"hello","--irq-stack",0};int r=app_main(2,args,0,&s);
 if(mode==1)assert(r==241 && calls==1 && !writes);
 else if(mode==2)assert(r==244 && calls==2 && !writes);
 else assert(!r && calls==4 && writes==1);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'tests/mm/boundary_hello.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 for mode in range(3):subprocess.run([str(p/'test'),str(mode)],check=True)
print('IRQ stack app: deterministic CPU/stack workload, uptime wrap and failure reporting PASS')
