#include "../../src/apps/hello/abi.h"
#include "../../src/apps/lib/io.h"
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
static int owner_exec(const struct cp32_app_services *s)
{
  unsigned pending,mask,zero=0;
  if(cp32_getpgrp()!=s->getpid() || cp32_sigpending(&pending) || pending!=CP32_SIGTERM_MASK ||
     cp32_sigprocmask(2,0,&mask) || mask!=CP32_SIGTERM_MASK ||
     cp32_signal(CP32_SIGUSR1,CP32_SIG_IGN)!=CP32_SIG_IGN)return 231;
  if(cp32_signal(CP32_SIGTERM,CP32_SIG_IGN)!=CP32_SIG_DFL ||
     cp32_sigpending(&pending) || pending || cp32_sigprocmask(2,&zero,0))return 232;
  return 0;
}
static int owner_test(const struct cp32_app_services *s)
{
  unsigned mask=CP32_SIGTERM_MASK,zero=0,pending;
  int round,pid,status,group=cp32_getpgrp();
  if(cp32_signal(CP32_SIGUSR1,CP32_SIG_IGN)<0)return 233;
  for(round=0;round<2;round++) {
    if(cp32_sigprocmask(2,&mask,0) || cp32_signal(CP32_SIGTERM,CP32_SIG_DFL)<0 ||
       cp32_kill(s->getpid(),CP32_SIGTERM))return 234;
    pid=cp32_fork();if(pid<0)return 235;
    if(!pid) {
      const char *args[]={"hello","--owner-exec",0};unsigned inherited;
      if(cp32_getpgrp()!=group || cp32_sigpending(&pending) || pending ||
         cp32_sigprocmask(2,0,&inherited) || inherited!=mask ||
         cp32_signal(CP32_SIGUSR1,CP32_SIG_IGN)!=CP32_SIG_IGN)s->exit(236);
      if(cp32_setsid()!=s->getpid() || cp32_kill(s->getpid(),CP32_SIGTERM) || cp32_yield())s->exit(237);
      if(cp32_getpgrp()!=s->getpid() || cp32_sigpending(&pending) || pending!=mask)s->exit(238);
      cp32_execv("/boot/hello",args);s->exit(239);
    }
    if(cp32_getpgrp()!=group || cp32_sigpending(&pending) || pending!=mask ||
       cp32_signal(CP32_SIGTERM,CP32_SIG_IGN)!=CP32_SIG_DFL || cp32_sigprocmask(2,&zero,0))return 240;
    if(cp32_waitpid(pid,&status,0)!=pid || status || cp32_sigpending(&pending) || pending ||
       cp32_getpgrp()!=group)return 241;
  }
  return cp32_write(1,"Signal ownership OK\n",20)==20 ? 0:242;
}
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;
  if(!s || s->version<1 || s->version>36 || !s->write || !s->exit || started)return 1;
  started=1;
  if(option(argc,argv,"--t"))return CP32_HELLO_TEST_REQUEST;
  if(option(argc,argv,"--owner") || option(argc,argv,"--owner-exec")) {
    if(s->version<36 || cp32_io_init(s))return 90;
    return option(argc,argv,"--owner") ? owner_test(s):owner_exec(s);
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
