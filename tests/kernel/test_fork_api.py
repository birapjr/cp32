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
 assert(cp32_getpgrp()==-1 && cp32_errno==CP32_ENOSYS);
 s.version=27;result=123;assert(cp32_getpgrp()==123 && operation==CP32_APP_GETPGRP && !fd_value && !pointer && !options);
 assert(cp32_setsid()==123 && operation==CP32_APP_SETSID);
 result=-1;assert(cp32_setsid()==-1 && cp32_errno==1);
 result=123;assert(cp32_waitpid(-123,&status,0)==123 && fd_value==-123);
 struct cp32_app_times t;
 assert(cp32_times(&t)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=28;result=0;assert(cp32_times(&t)==0);
 assert(operation==CP32_APP_TIMES && pointer==&t && !fd_value && options==sizeof(t));
 assert(cp32_times(0)==-1 && cp32_errno==CP32_EFAULT);
 result=-14;assert(cp32_times(&t)==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_yield()==-1 && cp32_errno==CP32_ENOSYS);
 s.version=29;result=0;assert(!cp32_yield() && operation==CP32_APP_YIELD && !fd_value && !pointer && !options);
 result=-22;assert(cp32_yield()==-1 && cp32_errno==CP32_EINVAL);
 assert(cp32_kill(123,9)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=30;result=0;assert(!cp32_kill(123,CP32_SIGKILL));
 assert(operation==CP32_APP_KILL && fd_value==123 && !pointer && options==9);
 result=-3;assert(cp32_kill(123,0)==-1 && cp32_errno==3);
 result=-22;assert(cp32_kill(-1,-1)==-1 && cp32_errno==22);
 unsigned mask=1U<<14,old=99;
 assert(cp32_sigprocmask(0,&mask,&old)==-1 && cp32_errno==CP32_ENOSYS);
 assert(cp32_sigpending(&old)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=31;result=0;assert(!cp32_sigprocmask(CP32_SIG_BLOCK,&mask,&old) && !old);
 assert(operation==CP32_APP_SIGMASK && fd_value==0 && !pointer && options==mask);
 result=mask;assert(!cp32_sigprocmask(99,0,&old) && old==mask && fd_value==4 && !options);
 result=-22;old=99;assert(cp32_sigprocmask(2,&mask,&old)==-1 && old==99);
 result=mask;assert(!cp32_sigpending(&old) && old==mask && operation==CP32_APP_SIGPENDING && !fd_value && !pointer && !options);
 assert(cp32_sigpending(0)==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_signal(15,CP32_SIG_IGN)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=32;result=0;assert(cp32_signal(15,CP32_SIG_IGN)==0);
 assert(operation==CP32_APP_SIGNAL && fd_value==15 && !pointer && options==1);
 result=1;assert(cp32_signal(15,CP32_SIG_DFL)==1);
 result=-22;assert(cp32_signal(9,CP32_SIG_IGN)==-1 && cp32_errno==CP32_EINVAL);
 s.version=25;assert(cp32_waitpid(-1,0,0)==-1 && cp32_errno==CP32_ENOSYS);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'src/apps/lib/io.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('fork ABI: version gating, parent/child results, wait arguments and errno pass')
