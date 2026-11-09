#include "abi.h"
#include "../lib/io.h"
static const char greeting[]="Hello from a CP32 application!\n";
/* Keep BSS in the fixture so the eventual loader must zero it. */
static volatile unsigned started;
static __attribute__((noinline)) int option(unsigned argc,char **argv,const char *name)
{
  unsigned i=0;
  if(argc!=2) return 0;
  while(name[i] && name[i]==argv[1][i]) i++;
  return !name[i] && !argv[1][i];
}
/* Current process-lifecycle acceptance checks. */
static int fork_test(const struct cp32_app_services *s,int exec_child)
{
  volatile unsigned local=17;
  unsigned original=s->getpid();
  unsigned *memory=(unsigned *)s->sbrk(16);
  int fd,pid,status=-1;char expected[2],value,cwd[8];
  if(memory==(void *)-1)return 91;
  *memory=23;
  if(cp32_chdir("/"))return 104;cp32_umask(0022);
  fd=cp32_open("/readme");
  if(fd<0 || cp32_pread(fd,expected,2,0)!=2)return 92;
  pid=cp32_fork();
  if(pid<0)return 93;
  if(!pid) {
    if(s->getpid()==(int)original || s->getppid()!=(int)original ||
       cp32_read(fd,&value,1)!=1 || value!=expected[0])s->exit(94);
    if(cp32_close(fd) || cp32_chdir("/boot") || cp32_umask(0077)!=0022)s->exit(105);
    local=99;*memory=77;started=88;
    if(exec_child) {
      const char *args[]={"echo","Fork exec child",0};
      cp32_execv("/boot/echo",args);s->exit(96);
    }
    s->exit(7);
  }
  if(!cp32_getcwd(cwd,sizeof(cwd)) || cwd[0]!='/' || cwd[1] || cp32_umask(0022)!=0022)return 106;
  if(s->getpid()!=(int)original || local!=17 || *memory!=23 || started!=1)return 97;
  if(cp32_waitpid(pid+1,&status,0)!=-1 || cp32_errno!=10)return 98;
  if(cp32_waitpid(pid,&status,0)!=pid || status!=(exec_child ? 0:7<<8))return 99;
  if(cp32_waitpid(-1,&status,0)!=-1 || cp32_errno!=10)return 100;
  if(cp32_read(fd,&value,1)!=1 || value!=expected[1] || cp32_close(fd))return 101;
  if(s->sbrk(-16)==(void *)-1)return 102;
  return cp32_write(1,"Fork memory/file/wait OK\n",25)==25 ? 0:103;
}
/* Several exited children remain independently collectable. Short command
 * for the Cardputer keyboard; run twice to check parent-exit cleanup. */
static int wait_test(const struct cp32_app_services *s)
{
  int pids[8],i,pid,status;unsigned seen=0;
  for(i=0;i<8;i++) {
    pid=cp32_fork();if(pid<0)return 107;
    if(!pid)s->exit(i+1);
    pids[i]=pid;
  }
  if(cp32_fork()!=-1 || cp32_errno!=11)return 108;
  if(cp32_waitpid(pids[3],&status,CP32_WNOHANG)!=pids[3] || status!=(4<<8))return 109;
  pid=cp32_fork();if(pid<0)return 110;if(!pid)s->exit(42);
  if(cp32_waitpid(pid,&status,0)!=pid || status!=(42<<8))return 111;
  for(i=0;i<7;i++) {
    int j;
    pid=cp32_waitpid(-1,&status,0);
    for(j=0;j<8 && pids[j]!=pid;j++){}
    if(j==8 || j==3 || (seen&(1U<<j)) || status!=((j+1)<<8))return 112;
    seen|=1U<<j;
  }
  if(cp32_waitpid(-1,&status,0)!=-1 || cp32_errno!=10)return 113;
  /* Leave one status behind: parent exit must discard it. */
  pid=cp32_fork();if(pid<0)return 114;if(!pid)s->exit(0);
  return cp32_write(1,"Wait children OK\n",17)==17 ? 0:115;
}
static __attribute__((noinline)) int tree_test(const struct cp32_app_services *s,int depth)
{
  volatile int local=depth;
  int pid=cp32_fork(),status;
  if(pid<0)return 116;
  if(!pid) {
    int group=cp32_setsid();
    if(group<=0)s->exit(119);
    local=99;
    if(depth==2) {
      if(cp32_fork()!=-1)s->exit(117);
    } else if(tree_test(s,depth+1))s->exit(118);
    s->exit(9);
  }
  if(cp32_waitpid(0,&status,CP32_WNOHANG)!=-1 || cp32_errno!=10)return 120;
  return local!=depth || cp32_waitpid(-pid,&status,0)!=pid || status!=(9<<8);
}
/* Exercise actual tick charging and child accumulation, without a long command.
 * Bound the CPU-work check by uptime so a broken counter reports an error. */
static int times_test(const struct cp32_app_services *s)
{
  struct cp32_app_times before,after,now;
  volatile unsigned work=0;
  int pid,status;unsigned i;
  if(cp32_times(0)!=-1 || cp32_errno!=CP32_EFAULT || cp32_times(&before))return 121;
  pid=cp32_fork();if(pid<0)return 122;
  if(!pid) {
    if(cp32_times(&before) || before.children_user || before.children_system)s->exit(123);
    do {
      for(i=0;i<100000;i++)work+=i;
      if(cp32_times(&now))s->exit(124);
      if(now.uptime-before.uptime>3*CP32_CLK_TCK)s->exit(125);
    } while(now.user-before.user<2);
    s->exit(0);
  }
  if(cp32_times(&after) || after.children_user-before.children_user<2)return 126;
  if(cp32_waitpid(pid,&status,0)!=pid || status)return 127;
  if(cp32_times(&now) || now.children_user!=after.children_user ||
     now.children_system!=after.children_system)return 128;
  return cp32_write(1,"Times OK (60 Hz)\n",17)==17 ? 0:129;
}
/* Intentional hardware faults, isolated in child processes by --fault.
 * --crash exercises the foreground shell's terminal-status path directly. */
static __attribute__((noreturn,noinline)) void trigger_fault(int kind)
{
#if defined(__XTENSA__)
  if(kind==0)__asm__ volatile("ill");
  else if(kind==1)__asm__ volatile("movi a2,0\n\tl32i a2,a2,0" ::: "a2","memory");
  else if(kind==2)__asm__ volatile("movi a2,0\n\ts32i a2,a2,0" ::: "a2","memory");
  else __asm__ volatile("movi a1,0\n\till" ::: "memory");
  __builtin_unreachable();
#else
  (void)kind;__builtin_trap();
#endif
}
static int fault_test(const struct cp32_app_services *s)
{
  volatile unsigned local=77;
  int fd,pid,status,i;char byte;
  fd=cp32_open("/readme");if(fd<0)return 130;
  for(i=0;i<4;i++) {
    pid=cp32_fork();if(pid<0)return 131;
    if(!pid) {
      if(cp32_read(fd,&byte,1)!=1 || cp32_open("/readme")<0)s->exit(132);
      local=99;started=88;trigger_fault(i);
    }
    if(cp32_waitpid(pid,&status,0)!=pid || !CP32_WIFSIGNALED(status) ||
       CP32_WTERMSIG(status)!=((i==1 || i==2) ? CP32_SIGSEGV:CP32_SIGILL))return 133;
    if(local!=77 || started!=1 || cp32_lseek(fd,0,1)!=i+1)return 134;
  }
  if(cp32_close(fd))return 135;
  return cp32_write(1,"Fault recovery OK\n",18)==18 ? 0:136;
}
static int yield_test(const struct cp32_app_services *s,int mode)
{
  volatile unsigned local=17;
  unsigned *memory=(unsigned *)s->sbrk(16),original=s->getpid();
  int fd,pid,status=123;char byte,cwd[16];
  if(memory==(void *)-1 || cp32_chdir("/"))return 140;
  *memory=23;cp32_umask(0022);fd=cp32_open("/readme");if(fd<0)return 141;
  pid=cp32_fork();if(pid<0)return 142;
  if(!pid) {
    local=99;*memory=77;started=88;
    if(cp32_chdir("/boot") || cp32_setsid()!=s->getpid() || cp32_read(fd,&byte,1)!=1)s->exit(143);
    if(cp32_yield() || local!=99 || *memory!=77 || started!=88 ||
       s->sbrk(0)!=(void *)(memory+4) || s->getppid()!=(int)original)s->exit(144);
    if(!cp32_getcwd(cwd,sizeof(cwd)) || cwd[0]!='/' || cwd[1]!='b' ||
       cp32_getpgrp()!=s->getpid() || cp32_read(fd,&byte,1)!=1)s->exit(145);
    if(cp32_yield() || cp32_yield())s->exit(146);
    s->exit(7);
  }
  if(cp32_waitpid(pid,&status,CP32_WNOHANG)!=0 || status!=123)return 147;
  if(mode==1){cp32_write(1,"Yield exit OK\n",14);return 0;}
  if(mode==2){const char *args[]={"echo","Yield exec parent",0};cp32_execv("/boot/echo",args);return 148;}
  if(cp32_fork()!=-1 || cp32_errno!=11 || local!=17 || *memory!=23 || started!=1)return 149;
  if(s->sbrk(16)==(void *)-1 || cp32_yield())return 150;
  if(local!=17 || *memory!=23 || started!=1 || cp32_lseek(fd,0,1)!=2 ||
     !cp32_getcwd(cwd,sizeof(cwd)) || cwd[0]!='/' || cwd[1])return 151;
  if(cp32_waitpid(pid,&status,0)!=pid || status!=(7<<8) || cp32_close(fd) ||
     s->sbrk(0)!=(void *)(memory+8) || s->sbrk(-32)==(void *)-1)return 152;
  return cp32_write(1,"Yield pair OK\n",14)==14 ? 0:153;
}
static __attribute__((noinline)) int mask_test(const struct cp32_app_services *s)
{
    unsigned mask=CP32_SIGTERM_MASK|CP32_SIGKILL_MASK,old=99,pending;
    int pid,status;
    if(s->version<31 || cp32_io_init(s))return 90;
    if(cp32_sigprocmask(CP32_SIG_BLOCK,&mask,&old) || old)return 169;
    {
      pid=cp32_fork();if(pid<0)return 170;
      if(!pid) {
        if(cp32_sigprocmask(0,0,&old) || old!=CP32_SIGTERM_MASK ||
           cp32_sigpending(&pending) || pending)s->exit(171);
        if(cp32_yield())s->exit(172);
        if(cp32_sigpending(&pending) || pending!=CP32_SIGTERM_MASK)s->exit(173);
        mask=CP32_SIGTERM_MASK;
        cp32_sigprocmask(CP32_SIG_UNBLOCK,&mask,0);
        s->exit(177); /* unblocking pending default termination never returns */
      }
      if(cp32_kill(pid,CP32_SIGTERM) || cp32_kill(pid,CP32_SIGTERM) ||
         cp32_waitpid(pid,&status,CP32_WNOHANG)!=0)return 178;
      if(cp32_waitpid(pid,&status,0)!=pid || !CP32_WIFSIGNALED(status) ||
         CP32_WTERMSIG(status)!=CP32_SIGTERM)return 180;
    }
    mask=0;if(cp32_sigprocmask(CP32_SIG_SETMASK,&mask,&old) || old!=CP32_SIGTERM_MASK)return 182;
    return cp32_write(1,"Signal mask OK\n",15)==15 ? 0:183;
  }
static __attribute__((noinline)) int signals_test(const struct cp32_app_services *s)
{
    unsigned mask=CP32_SIGSET(CP32_SIGUSR1)|CP32_SIGSET(CP32_SIGUSR2),pending;
    int pid,status;
    if(s->version<33 || cp32_io_init(s))return 90;
    if(cp32_sigprocmask(0,&mask,0))return 192;
    pid=cp32_fork();if(pid<0)return 193;
    if(!pid) {
      cp32_yield();
      if(cp32_sigpending(&pending) || pending!=mask || cp32_signal(CP32_SIGUSR2,CP32_SIG_IGN))s->exit(194);
      if(cp32_sigpending(&pending) || pending!=CP32_SIGSET(CP32_SIGUSR1))s->exit(195);
      cp32_sigprocmask(1,&mask,0);s->exit(196);
    }
    if(cp32_kill(pid,CP32_SIGUSR2) || cp32_kill(pid,CP32_SIGUSR1) ||
       cp32_waitpid(pid,&status,0)!=pid || status!=CP32_SIGUSR1)return 197;
    return cp32_write(1,"Signals OK\n",11)==11 ? 0:199;
  }
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;

  if(!s || (s->version<1 || s->version>33) || !s->write || !s->exit || started) return 1;
  started=1;
  if(option(argc,argv,"--t"))return CP32_HELLO_TEST_REQUEST;
  if(option(argc,argv,"--signals"))return signals_test(s);
  if(option(argc,argv,"--igexec")) {
    if(s->version<32 || cp32_io_init(s))return 90;
    return cp32_signal(15,CP32_SIG_IGN)==CP32_SIG_IGN && !cp32_kill(s->getpid(),15) ? 0:190;
  }
  if(option(argc,argv,"--ignore")) {
    unsigned mask=CP32_SIGTERM_MASK,pending;int pid,status;
    if(s->version<32 || cp32_io_init(s))return 90;
    if(cp32_sigprocmask(0,&mask,0) || cp32_kill(s->getpid(),15) ||
       cp32_signal(15,CP32_SIG_IGN)!=CP32_SIG_DFL || cp32_sigpending(&pending) || pending)return 184;
    mask=0;if(cp32_sigprocmask(2,&mask,0))return 185;
    pid=cp32_fork();if(pid<0)return 186;
    if(!pid){const char *args[]={"hello","--igexec",0};
      if(cp32_kill(s->getpid(),15))s->exit(187);cp32_yield();
      cp32_execv("/boot/hello",args);s->exit(191);}
    if(cp32_kill(pid,15) || cp32_waitpid(pid,&status,CP32_WNOHANG)!=0 ||
       cp32_waitpid(pid,&status,0)!=pid || status ||
       cp32_signal(15,CP32_SIG_DFL)!=CP32_SIG_IGN)return 188;
    return cp32_write(1,"Signal ignore OK\n",17)==17 ? 0:189;
  }
  if(option(argc,argv,"--mask"))return mask_test(s);
  if(option(argc,argv,"--kill")) {
    int pid,status,signal,round,fd;char byte;
    if(s->version<30 || cp32_io_init(s))return 90;
    fd=cp32_open("/readme");if(fd<0)return 159;
    if(cp32_kill(s->getpid(),0) || cp32_kill(0,9)!=-1 || cp32_errno!=22)return 160;
    for(round=0;round<3;round++) {
      signal=round==0 ? CP32_SIGKILL:CP32_SIGTERM;
      pid=cp32_fork();if(pid<0)return 161;
      if(!pid) {
        if(cp32_read(fd,&byte,1)!=1)s->exit(162);
        if(round==2)cp32_kill(s->getpid(),signal);
        else cp32_yield();
        s->exit(163); /* successful delivery must never resume the target */
      }
      if(round<2 && (cp32_kill(pid,0) || cp32_waitpid(pid,&status,CP32_WNOHANG)!=0 ||
          cp32_kill(pid,signal)))return 164;
      if(cp32_kill(pid,0) || cp32_waitpid(pid,&status,0)!=pid ||
         !CP32_WIFSIGNALED(status) || CP32_WTERMSIG(status)!=signal)return 165;
      if(cp32_kill(pid,0)!=-1 || cp32_errno!=3 || cp32_lseek(fd,0,1)!=round+1)return 166;
    }
    if(cp32_close(fd))return 167;
    return cp32_write(1,"Kill wait OK\n",13)==13 ? 0:168;
    }
  if(option(argc,argv,"--ytree")) {
    int pid,status=123,r;
    if(s->version<29 || cp32_io_init(s))return 90;
    pid=cp32_fork();if(pid<0)return 154;
    if(!pid) {
      r=yield_test(s,0);
      if(r)s->exit(r);
      if(cp32_yield())s->exit(155);
      s->exit(0);
    }
    if(started!=1 || cp32_waitpid(pid,&status,CP32_WNOHANG)!=0 || status!=123)return 156;
    if(cp32_waitpid(pid,&status,0)!=pid || status)return 157;
    return cp32_write(1,"Yield tree OK\n",14)==14 ? 0:158;
  }
  if(option(argc,argv,"--yield") || option(argc,argv,"--yexit") || option(argc,argv,"--yexec")) {
    if(s->version<29 || cp32_io_init(s))return 90;
    return yield_test(s,option(argc,argv,"--yexit") ? 1:option(argc,argv,"--yexec") ? 2:0);
  }
  if(option(argc,argv,"--crash"))trigger_fault(0);
  if(option(argc,argv,"--fault")) {
    if(cp32_io_init(s))return 90;
    return fault_test(s);
  }
  if(option(argc,argv,"--time")) {
    if(cp32_io_init(s))return 90;
    return times_test(s);
  }
  if(option(argc,argv,"--tree")) {
    int r;if(cp32_io_init(s))return 90;
    r=tree_test(s,1);if(!r)cp32_write(1,"Fork tree OK\n",13);return r;
  }
  if(option(argc,argv,"--wait")) {
    if(s->version<26 || cp32_io_init(s))return 90;
    return wait_test(s);
  }
  if(option(argc,argv,"--fork") || option(argc,argv,"--fork-exec")) {
    if(cp32_io_init(s))return 90;
    return fork_test(s,option(argc,argv,"--fork-exec"));
  }
  if(option(argc,argv,"--exec") || option(argc,argv,"--exec-fail")) {
    int pid;char a[8];
    const char *next[]={"hello","--exec-child",0};
    if(cp32_io_init(s) || !s->getpid || (pid=s->getpid())<=0)return 1;
    if(cp32_execv("/missing",next)!=-1 || cp32_errno!=CP32_ENOENT || s->getpid()!=pid)return 1;
    if(cp32_execv("/readme",next)!=-1 || cp32_errno!=CP32_EACCES)return 1;
    if(option(argc,argv,"--exec-fail"))return cp32_write(1,"Exec failure recovery OK\n",24)==24 ? 0:1;
    if(cp32_open("/readme")!=3 || cp32_read(3,a,5)!=5)return 1;
    /* Pass the original PID as an argv string; next image verifies identity. */
    char id[12];unsigned n=0,value=pid,j;
    do {id[n++]='0'+value%10;value/=10;}while(value);
    for(j=0;j<n/2;j++){char c=id[j];id[j]=id[n-1-j];id[n-1-j]=c;}id[n]=0;
    const char *args[]={"hello","--exec-child",id,0};
    cp32_execv("/boot/hello",args);return 1;
  }
  if(argc==3 && option(2,argv,"--exec-child")) {
    unsigned pid=0,j;char a[8];
    for(j=0;argv[2][j];j++) {if(argv[2][j]<'0' || argv[2][j]>'9')return 1;pid=pid*10+argv[2][j]-'0';}
    if(cp32_io_init(s) || !s->getpid || (unsigned)s->getpid()!=pid ||
       cp32_lseek(3,0,1)!=5 || cp32_read(3,a,5)!=5 || cp32_close(3))return 1;
    return cp32_write(1,"Exec PID and file OK\n",20)==20 ? 0:1;
  }
  if(option(argc,argv,"--line")) {
    char line[16];int count;
    if(cp32_io_init(s) || cp32_write(1,"Line: ",6)!=6) return 1;
    do {
      count=cp32_readline(line,sizeof(line));
      if(count<0) return 1;
      if(cp32_write(1,line,(unsigned)count)!=count) return 1;
    } while(count && line[count-1]!='\n');
    return 0;
  }
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='p' &&
     ((argv[1][3]=='i' && argv[1][4]=='d' && !argv[1][5]) ||
      (argv[1][3]=='p' && argv[1][4]=='i' && argv[1][5]=='d' && !argv[1][6]))) {
    char text[12]; unsigned value,pos=sizeof(text);
    int pid,parent=argv[1][3]=='p';
    if(parent) {
      if(s->version<3 || !s->getppid || (pid=s->getppid())<=0) return 1;
    } else if(s->version<2 || !s->getpid || (pid=s->getpid())<=0) return 1;
    value=(unsigned)pid; text[--pos]='\n';
    do { text[--pos]='0'+value%10; value/=10; } while(value);
    if(s->write(parent ? "PPID=" : "PID=",parent ? 5 : 4)!=(parent ? 5 : 4) ||
       s->write(text+pos,sizeof(text)-pos)!=(int)(sizeof(text)-pos)) return 1;
    return 0;
  }
  /* Exercise explicit exit, independently of crt0's main-return path.
   * Bound decimal parsing without depending on a hosted libc. */
  if(argc>1 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='e' && argv[1][3]=='x' && argv[1][4]=='i' &&
     argv[1][5]=='t' && argv[1][6]==0) {
    unsigned value=0;
    if(argc!=3 || !argv[2][0]) goto usage;
    for(n=0;argv[2][n];n++) {
      unsigned digit=(unsigned char)argv[2][n]-'0';
      if(digit>9 || value>(65535U-digit)/10U) goto usage;
      value=value*10+digit;
    }
    s->exit((int)value);
    return 1; /* A broken service must not fall through to the greeting. */
  }
  if(option(argc,argv,"--env")) {
    for(i=0;envp && envp[i];i++) {
      for(n=0;envp[i][n];n++) {}
      if(s->write(envp[i],n)!=(int)n || s->write("\n",1)!=1) return 1;
    }
    return 0;
  }
  if(s->write(greeting,sizeof(greeting)-1)!=(int)(sizeof(greeting)-1)) return 1;
  for(i=1;i<argc;i++) {
    for(n=0;argv[i][n];n++) {}
    if(s->write("arg: ",5)!=5 || s->write(argv[i],n)!=(int)n ||
       s->write("\n",1)!=1) return 1;
  }
  return 0;
usage:
  s->write("Usage: hello --exit 0..65535\n",
           sizeof("Usage: hello --exit 0..65535\n")-1);
  return 2;
}
