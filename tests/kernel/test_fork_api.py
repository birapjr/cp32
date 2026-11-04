"""ABI wrappers: version gating, fork results, wait arguments and errno."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include <assert.h>
#include "io.h"
static int result,operation,fd_value;static unsigned options;static void *pointer;
static int rd(char *p,unsigned n){(void)p;(void)n;return 0;}
static int wr(const char *p,unsigned n){(void)p;return n;}
static int file(unsigned op,int fd,void *p,unsigned n){operation=op;fd_value=fd;pointer=p;options=n;return result;}
int main(void){
 struct cp32_app_services s={.version=25,.read=rd,.write=wr,.file=file};int status;
 assert(cp32_fork()==-1 && cp32_errno==CP32_ENOSYS);
 assert(!cp32_io_init(&s));assert(cp32_fork()==-1 && cp32_errno==CP32_ENOSYS);
 s.version=26;result=0;assert(cp32_fork()==0 && operation==CP32_APP_FORK && !pointer && !options);
 result=123;assert(cp32_fork()==123);
 result=-11;assert(cp32_fork()==-1 && cp32_errno==11);
 result=123;assert(cp32_waitpid(123,&status,CP32_WNOHANG)==123);
 assert(operation==CP32_APP_WAITPID && fd_value==123 && pointer==&status && options==1);
 result=-10;assert(cp32_waitpid(-1,0,0)==-1 && cp32_errno==10);
 s.version=25;assert(cp32_waitpid(-1,0,0)==-1 && cp32_errno==CP32_ENOSYS);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('fork ABI: version gating, parent/child results, wait arguments and errno pass')
