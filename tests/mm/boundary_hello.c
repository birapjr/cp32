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
/* Removable test diagnostic; --t routes application output only to USB. */
static int fs_open_error(void)
{
  char text[12];unsigned value=(unsigned)cp32_errno,pos=sizeof(text);
  text[--pos]='\n';
  do {text[--pos]='0'+value%10;value/=10;} while(value);
  cp32_write(1,"[FS V202 open errno] ",sizeof("[FS V202 open errno] ")-1);
  cp32_write(1,text+pos,sizeof(text)-pos);
  return 232;
}
/* Exercise actual timer entry while this application's C stack is live. */
static int irq_stack_test(void)
{
  volatile unsigned guard[32],value=1;unsigned i,expected=1,passes=0;
  struct cp32_app_times first,now;
  for(i=0;i<32;i++)guard[i]=0xa55a0000U+i;
  if(cp32_times(&first)<0)return 241;
  do {
    for(i=0;i<50000;i++)value=value*1664525U+1013904223U;
    for(i=0;i<50000;i++)expected=expected*1664525U+1013904223U;
    if(value!=expected)return 242;
    for(i=0;i<32;i++)if(guard[i]!=0xa55a0000U+i)return 243;
    if(cp32_times(&now)<0)return 244;
    if(++passes>100000)return 245;
  } while(now.uptime-first.uptime<12);
  return cp32_write(1,"IRQ stack OK\n",13)==13 ? 0:246;
}
static int fs_owner_test(const struct cp32_app_services *s)
{
  char cwd[16],byte;int fd,pid,status,round;
  if(cp32_chdir("/") || cp32_umask(0022)<0)return 231;
  for(round=0;round<2;round++) {
    /* The running executable exists and contains at least an ELF header. */
    fd=cp32_open("/boot/hello");if(fd<3)return fs_open_error();
    pid=cp32_fork();if(pid<0)return 233;
    if(!pid) {
      if(cp32_chdir("/boot") || cp32_umask(0077)!=0022 ||
         cp32_read(fd,&byte,1)!=1)s->exit(234);
      if(cp32_yield()<0)s->exit(235);
      if(!cp32_getcwd(cwd,sizeof(cwd)) || cwd[0]!='/' || cwd[1]!='b' ||
         cp32_umask(0077)!=0077 || cp32_close(fd))s->exit(236);
      s->exit(0);
    }
    if(!cp32_getcwd(cwd,sizeof(cwd)) || cwd[0]!='/' || cwd[1] ||
       cp32_umask(0022)!=0022 || cp32_lseek(fd,0,1)!=1)return 237;
    if(cp32_waitpid(pid,&status,0)!=pid || status)return 238;
    if(cp32_read(fd,&byte,1)!=1 || cp32_close(fd))return 239;
  }
  return cp32_write(1,"FS ownership OK\n",16)==16 ? 0:240;
}
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;
  if(!s || s->version<1 || s->version>36 || !s->write || !s->exit || started)return 1;
  started=1;
  if(option(argc,argv,"--t"))return CP32_HELLO_TEST_REQUEST;
  if(option(argc,argv,"--irq-stack")) {
    if(s->version<36 || cp32_io_init(s))return 90;
    return irq_stack_test();
  }
  if(option(argc,argv,"--fs-owner")) {
    if(s->version<36 || cp32_io_init(s))return 90;
    return fs_owner_test(s);
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
