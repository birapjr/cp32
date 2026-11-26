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
/* Current concurrent-process acceptance suite. No manual input or files. */
static volatile unsigned received,acknowledged;
static void receive_signal(int sig)
{
  if(sig==CP32_SIGUSR1)received++;
  if(sig==CP32_SIGUSR2)acknowledged++;
}
static int handlers(void)
{
  received=acknowledged=0;
  return cp32_signal_handler(CP32_SIGUSR1,receive_signal)<0 ||
         cp32_signal_handler(CP32_SIGUSR2,receive_signal)<0;
}
static void burn(unsigned count)
{
  volatile unsigned value=17;
  while(count--)value=value*1664525U+1013904223U;
}
static int collect(int pid,int expected)
{
  int status,r;
  do {r=cp32_waitpid(pid,&status,0);} while(r<0 && cp32_errno==CP32_EINTR);
  return r==pid && status==expected ? 0:-1;
}
static int wait_signal(volatile unsigned *value)
{
  unsigned spins=0;
  /* No yield, IPC or clock query here: progress requires timer preemption. */
  while(!*value)if(++spins==100000000U)return -1;
  return 0;
}
static int process_test(const struct cp32_app_services *s)
{
  int children[8],n,pid,i,round,error,capacity=-1,parent=s->getpid();
  struct cp32_app_times begin,end;
  volatile unsigned canary=0x13579bdfU;
  if(handlers() || cp32_times(&begin))return 210;
  pid=cp32_fork();if(pid<0)return 211;
  if(!pid) {
    canary=0x2468ace0U;
    if(wait_signal(&acknowledged))s->exit(1);
    if(cp32_times(&begin))s->exit(4);
    burn(2000000);
    if(cp32_times(&end) || end.user==begin.user)s->exit(5);
    if(cp32_kill(parent,CP32_SIGUSR1))s->exit(2);
    s->exit(canary==0x2468ace0U ? 7:3);
  }
  if(cp32_kill(pid,CP32_SIGUSR2) || wait_signal(&received) ||
     canary!=0x13579bdfU || cp32_times(&end) || end.user==begin.user ||
     collect(pid,7<<8))return 212;
  /* Two independently live siblings, then repeated memory/slot exhaustion.
   * Children spin in application code until SIGKILL; parent never yields. */
  for(round=0;round<2;round++) {
    for(n=0;n<8;n++) {
      pid=cp32_fork();
      if(pid<0)break;
      if(!pid){for(;;)burn(10000);}
      children[n]=pid;
    }
    error=n<2 || (n<8 && cp32_errno!=CP32_ENOMEM && cp32_errno!=CP32_EAGAIN) ? 213:0;
    if(capacity>=0 && capacity!=n)error=214;
    capacity=n;
    for(i=0;i<n;i++)if(cp32_waitpid(children[i],0,CP32_WNOHANG)!=0)error=215;
    for(i=0;i<n;i++)if(cp32_kill(children[i],CP32_SIGKILL))error=216;
    for(i=n-1;i>=0;i--)if(collect(children[i],CP32_SIGKILL))error=217;
    if(error)return error;
    if(cp32_waitpid(-1,0,CP32_WNOHANG)!=-1 || cp32_errno!=CP32_ECHILD)return 218;
  }
  pid=cp32_fork();if(pid<0)return 219;if(!pid)s->exit(8);
  if(collect(pid,8<<8))return 220;
  return cp32_write(1,"Concurrent process OK\n",22)==22 ? 0:221;
}
static int signal_sender(const struct cp32_app_services *s,int parent)
{
  struct cp32_app_times begin,now;
  acknowledged=0;
  if(cp32_times(&begin))s->exit(1);
  do {
    burn(100000);
    if(cp32_kill(parent,CP32_SIGUSR1))s->exit(2);
    if(cp32_times(&now))s->exit(3);
    if(now.uptime-begin.uptime>300)s->exit(4);
  } while(!acknowledged);
  s->exit(0);return 0;
}
static int signal_test(const struct cp32_app_services *s)
{
  unsigned mask=CP32_SIGSET(CP32_SIGUSR1),pending,previous;
  int pid,status,parent=s->getpid();char byte;
  volatile unsigned canary=0x12345678U;
  if(handlers() || cp32_sigprocmask(CP32_SIG_BLOCK,&mask,&previous))return 222;
  if(cp32_kill(parent,CP32_SIGUSR1) || cp32_kill(parent,CP32_SIGUSR1) ||
     received || cp32_sigpending(&pending) || !(pending&mask))return 223;
  if(cp32_sigprocmask(CP32_SIG_UNBLOCK,&mask,0) || received!=1 || canary!=0x12345678U)return 224;
  if(cp32_signal(CP32_SIGUSR1,CP32_SIG_IGN)<0 || cp32_kill(parent,CP32_SIGUSR1) ||
     cp32_sigpending(&pending) || (pending&mask) || received!=1 ||
     cp32_signal_handler(CP32_SIGUSR1,receive_signal)<0)return 225;
  if(cp32_signal(CP32_SIGKILL,CP32_SIG_IGN)!=-1 || cp32_errno!=CP32_EINVAL)return 226;
  /* Child keeps signaling until acknowledgement, avoiding a read/kill race. */
  pid=cp32_fork();if(pid<0)return 227;if(!pid)return signal_sender(s,parent);
  if(cp32_read(0,&byte,1)!=-1 || cp32_errno!=CP32_EINTR)return 228;
  if(cp32_kill(pid,CP32_SIGUSR2) || collect(pid,0))return 229;
  pid=cp32_fork();if(pid<0)return 230;if(!pid)return signal_sender(s,parent);
  if(cp32_waitpid(pid,&status,0)!=-1 || cp32_errno!=CP32_EINTR)return 231;
  if(cp32_kill(pid,CP32_SIGUSR2) || collect(pid,0) || canary!=0x12345678U)return 232;
  return cp32_write(1,"Async signals OK\n",17)==17 ? 0:233;
}
static int orphan_test(const struct cp32_app_services *s)
{
  int root=s->getpid(),pid,grandchild,ppid;unsigned spins=0;
  if(handlers())return 234;
  pid=cp32_fork();if(pid<0)return 235;
  if(!pid) {
    grandchild=cp32_fork();if(grandchild<0)s->exit(1);
    if(grandchild)s->exit(3);
    do {ppid=s->getppid();if(++spins>10000)s->exit(2);} while(ppid!=1);
    if(cp32_kill(root,CP32_SIGUSR1))s->exit(4);
    s->exit(9);
  }
  if(collect(pid,3<<8) || wait_signal(&received))return 236;
  if(cp32_waitpid(-1,0,CP32_WNOHANG)!=-1 || cp32_errno!=CP32_ECHILD)return 237;
  return cp32_write(1,"INIT adoption OK\n",17)==17 ? 0:238;
}
static void decimal(char *text,unsigned value)
{
  unsigned count=0,i;char reverse[12];
  do {reverse[count++]='0'+value%10U;value/=10U;} while(value);
  for(i=0;i<count;i++)text[i]=reverse[count-i-1];
  text[count]=0;
}
static int number(const char *text)
{
  unsigned value=0,digit;
  if(!*text)return -1;
  while(*text) {
    digit=(unsigned char)*text++-'0';
    if(digit>9 || value>(30000U-digit)/10U)return -1;
    value=value*10+digit;
  }
  return (int)value;
}
static int exec_child(const struct cp32_app_services *s,char **argv)
{
  int fd=number(argv[2]),pid=number(argv[3]),parent=number(argv[4]);char a,b;
  if(fd<3 || fd>6 || pid!=s->getpid() || parent!=s->getppid() ||
     cp32_kill(parent,0) || cp32_lseek(fd,0,1)!=2 ||
     cp32_pread(fd,&a,1,2)!=1 || cp32_read(fd,&b,1)!=1 || a!=b ||
     cp32_signal(CP32_SIGUSR1,CP32_SIG_DFL)!=CP32_SIG_DFL ||
     cp32_signal(CP32_SIGUSR2,CP32_SIG_IGN)!=CP32_SIG_IGN ||
     cp32_close(fd) || cp32_kill(parent,CP32_SIGUSR1))return 249;
  return 0;
}
static int io_exec_test(const struct cp32_app_services *s)
{
  char original[64],buffer[64],cwd[16],fdtext[12],pidtext[12],parenttext[12];
  const char *args[]={"hello","--exec-child",fdtext,pidtext,parenttext,0};
  volatile unsigned canary=0x1a2b3c4dU;
  int fd,pid,parent=s->getpid();unsigned round,i;
  if(handlers() || cp32_chdir("/") || (fd=cp32_open("/boot/hello"))<3 ||
     cp32_pread(fd,original,64,0)!=64 || cp32_read(fd,buffer,1)!=1)return 239;
  pid=cp32_fork();if(pid<0)return 240;
  if(!pid) {
    canary=0x5e6f7081U;
    if(cp32_chdir("/boot") || cp32_read(fd,buffer,1)!=1 ||
       cp32_lseek(fd,0,1)!=2)s->exit(241);
  }
  /* Both processes issue transfers through the same open file description.
   * Their fixed-address request/output buffers contain different canaries. */
  for(round=0;round<16;round++) {
    for(i=0;i<64;i++)buffer[i]=(char)(pid ? 0x55:0xaa);
    if(cp32_pread(fd,buffer,64,0)!=64)return 242;
    for(i=0;i<64;i++)if(buffer[i]!=original[i])return 243;
    burn(50000);
  }
  if(!pid) {
    decimal(fdtext,(unsigned)fd);decimal(pidtext,(unsigned)s->getpid());
    decimal(parenttext,(unsigned)parent);
    if(canary!=0x5e6f7081U || !cp32_getcwd(cwd,sizeof(cwd)) || cwd[1]!='b' ||
       cp32_execv("/boot/.cp32-noexec",args)!=-1 || cp32_errno!=CP32_ENOENT)return 244;
    if(cp32_signal(CP32_SIGUSR2,CP32_SIG_IGN)<0)return 245;
    cp32_execv("/boot/hello",args);return 246;
  }
  if(collect(pid,0) || !received || canary!=0x1a2b3c4dU ||
     !cp32_getcwd(cwd,sizeof(cwd)) || cwd[0]!='/' || cwd[1] ||
     cp32_lseek(fd,0,1)!=3 || cp32_close(fd))return 247;
  return cp32_write(1,"Concurrent I/O exec OK\n",23)==23 ? 0:248;
}
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;
  if(!s || s->version<1 || s->version>37 || !s->write || !s->exit || started)return 1;
  started=1;
  if(option(argc,argv,"--t"))return CP32_HELLO_TEST_REQUEST;
  if(option(argc,argv,"--process") || option(argc,argv,"--signals") || option(argc,argv,"--orphan") ||
     option(argc,argv,"--io-exec") || (argc==5 && option(2,argv,"--exec-child"))) {
    if(s->version<37 || !s->getpid || !s->getppid || cp32_io_init(s))return 90;
    if(option(argc,argv,"--process"))return process_test(s);
    if(option(argc,argv,"--signals"))return signal_test(s);
    if(option(argc,argv,"--io-exec"))return io_exec_test(s);
    if(argc==5)return exec_child(s,argv);
    return orphan_test(s);
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
