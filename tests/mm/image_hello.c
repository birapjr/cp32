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
/* Exercise image ownership through nested saves, repeated exchanges, exec
 * of the deepest child, restoration and a second complete reuse cycle. */
static int image_level(const struct cp32_app_services *s,unsigned level)
{
  volatile unsigned local=0x1234+level;
  unsigned *word=s->sbrk(16);int pid=s->getpid(),child,status,i;
  if(word==(void *)-1)return 231;
  *word=0x5678+level;started=level+1;
  if(level)for(i=0;i<3;i++) {
    if(cp32_yield() || local!=0x1234+level || *word!=0x5678+level ||
       started!=level+1 || s->getpid()!=pid)return 232;
  }
  if(level==2) {
    const char *args[]={"hello","--image-exec",0};
    cp32_execv("/boot/hello",args);return 233;
  }
  child=cp32_fork();if(child<0)return 234;
  if(!child)s->exit(image_level(s,level+1));
  for(i=0;i<3;i++) {
    if(local!=0x1234+level || *word!=0x5678+level || started!=level+1 ||
       s->getpid()!=pid || cp32_yield())return 235;
  }
  if(cp32_waitpid(child,&status,0)!=child || status || local!=0x1234+level ||
     *word!=0x5678+level || started!=level+1 || s->getpid()!=pid)return 236;
  if(s->sbrk(-16)==(void *)-1)return 237;
  return 0;
}
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;
  if(!s || s->version<1 || s->version>36 || !s->write || !s->exit || started)return 1;
  started=1;
  if(option(argc,argv,"--t"))return CP32_HELLO_TEST_REQUEST;
  if(option(argc,argv,"--image-exec"))return 0;
  if(option(argc,argv,"--image")) {
    int r;
    if(s->version<36 || cp32_io_init(s))return 90;
    r=image_level(s,0);if(!r)r=image_level(s,0);
    if(r)return r;
    return cp32_write(1,"Image ownership OK\n",19)==19 ? 0:238;
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
