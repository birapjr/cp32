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
static void decimal(char *out,unsigned long value)
{
  char reverse[24];unsigned n=0,i;
  do {reverse[n++]='0'+value%10;value/=10;}while(value);
  for(i=0;i<n;i++)out[i]=reverse[n-i-1];out[n]=0;
}
static unsigned long number(const char *p)
{
  unsigned long n=0;while(*p>='0' && *p<='9')n=n*10+*p++-'0';return n;
}
/* Process records must remain distinct while their images trade places. */
static int state_test(const struct cp32_app_services *s)
{
  unsigned long original=(unsigned long)s->sbrk(0);
  unsigned parent=s->getpid();
  int round,pid,status;
  for(round=0;round<2;round++) {
    unsigned *word=s->sbrk(16);if(word==(void *)-1)return 241;
    *word=17;
    pid=cp32_fork();if(pid<0)return 242;
    if(!pid) {
      unsigned long own;char pp[24],old[24];const char *args[]={"hello","--state-exec",pp,old,0};
      if(s->getppid()!=(int)parent || s->sbrk(32)==(void *)-1)s->exit(243);
      own=(unsigned long)s->sbrk(0);*word=99;pp[0]=old[0]=0;
      if(cp32_yield() || (unsigned long)s->sbrk(0)!=own || *word!=99 || s->getppid()!=(int)parent)s->exit(244);
      if(cp32_execv("/missing",args)!=-1 || (unsigned long)s->sbrk(0)!=own)s->exit(245);
      if(s->sbrk(2048)==(void *)-1)s->exit(246);
      decimal(pp,parent);decimal(old,(unsigned long)s->sbrk(0));
      cp32_execv("/boot/hello",args);s->exit(247);
    }
    if(*word!=17 || (unsigned long)s->sbrk(0)!=(unsigned long)word+16 || s->sbrk(64)==(void *)-1)return 248;
    if(cp32_waitpid(pid,&status,0)!=pid || status || *word!=17 ||
       (unsigned long)s->sbrk(0)!=(unsigned long)word+80)return 249;
    if(s->sbrk(-80)==(void *)-1 || (unsigned long)s->sbrk(0)!=original)return 250;
  }
  return cp32_write(1,"Process state OK\n",17)==17 ? 0:251;
}
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;
  if(!s || s->version<1 || s->version>36 || !s->write || !s->exit || started)return 1;
  started=1;
  if(option(argc,argv,"--t"))return CP32_HELLO_TEST_REQUEST;
  if(argc==4 && option(2,argv,"--state-exec")) {
    if(s->version<36 || !s->sbrk || !s->getppid)return 90;
    unsigned long initial=(unsigned long)s->sbrk(0);
    if(s->getppid()!=(int)number(argv[2]) || initial>=number(argv[3]) ||
       s->sbrk(16)==(void *)-1 || (unsigned long)s->sbrk(0)!=initial+16)return 252;
    return 0;
  }
  if(option(argc,argv,"--state")) {
    if(s->version<36 || cp32_io_init(s))return 90;
    return state_test(s);
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
