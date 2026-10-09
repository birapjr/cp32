#include "abi.h"
#include "../lib/heap.h"
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
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;

  if(!s || (s->version<1 || s->version>8) || !s->write || !s->exit || started) return 1;
  started=1;
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
  if(option(argc,argv,"--files")) {
    int fd[4],k;char a[8],b[8];
    if(cp32_io_init(s))return 1;
    for(k=0;k<4;k++)if((fd[k]=cp32_open("/readme"))!=k+3)return 1;
    if(cp32_open("/readme")!=-1 || cp32_errno!=CP32_EMFILE)return 1;
    if(cp32_read(fd[0],a,5)!=5 || cp32_read(fd[1],b,5)!=5)return 1;
    for(k=0;k<5;k++)if(a[k]!=b[k])return 1;
    if(cp32_lseek(fd[0],0,1)!=5 || cp32_lseek(fd[1],0,1)!=5 ||
       cp32_lseek(fd[0],-1,2)!=60 || cp32_read(fd[0],a,8)!=1 || a[0]!='\n' ||
       cp32_read(fd[0],a,8)!=0)return 1;
    if(cp32_close(fd[0]) || cp32_close(fd[0])!=-1 || cp32_errno!=CP32_EBADF)return 1;
    if(cp32_open("/missing")!=-1 || cp32_errno!=CP32_ENOENT)return 1;
    if(cp32_open("/boot")!=-1 || cp32_errno!=CP32_EISDIR)return 1;
    /* Deliberately leave three handles to exercise normal-exit cleanup. */
    return cp32_write(1,"File API OK\n",12)==12 ? 0 : 1;
  }
  if(option(argc,argv,"--errno")) {
    char byte;
    if(cp32_io_init(s)) return 1;
    if(cp32_read(1,&byte,1)!=-1 || cp32_errno!=CP32_EBADF ||
       cp32_write(1,0,1)!=-1 || cp32_errno!=CP32_EFAULT ||
       cp32_readline(&byte,1)!=-1 || cp32_errno!=CP32_EINVAL ||
       cp32_write(1,0,0)!=0 || cp32_errno!=CP32_EINVAL) return 1;
    return cp32_write(1,"I/O errno OK\n",13)==13 ? 0 : 1;
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
  if(option(argc,argv,"--io")) {
    char line[64];int count;
    if(cp32_io_init(s) || cp32_write(1,"Standard input: ",16)!=16) return 1;
    count=cp32_read(0,line,sizeof(line));
    if(count<0 || cp32_write(2,"Read: ",6)!=6 ||
       cp32_write(1,line,(unsigned)count)!=count) return 1;
    if(cp32_read(1,line,1)!=-1 || cp32_write(0,line,1)!=-1) return 1;
    return 0;
  }
  if(option(argc,argv,"--read")) {
    char line[64];int count;
    if(s->version<5 || !s->read) return 1;
    if(s->write("Type a line: ",13)!=13) return 1;
    count=s->read(line,sizeof(line));
    if(count<0 || count>(int)sizeof(line)) return 1;
    if(s->write("Read: ",6)!=6 || s->write(line,(unsigned)count)!=count) return 1;
    return 0;
  }
  if(option(argc,argv,"--trim")) {
    void *base,*a,*b,*top;
    if(cp32_heap_init(s)) return 1;
    base=s->sbrk(0);a=cp32_malloc(32);top=s->sbrk(0);b=cp32_malloc(64);
    if(!a || !b || cp32_heap_trim()!=0) return 1;
    cp32_free(b);
    if(cp32_heap_trim()<=0 || s->sbrk(0)!=top) return 1;
    cp32_free(a);
    if(cp32_heap_trim()<=0 || s->sbrk(0)!=base || cp32_heap_trim()!=0) return 1;
    a=cp32_malloc(32);if(!a) return 1;cp32_free(a);
    if(cp32_heap_trim()<=0 || s->sbrk(0)!=base) return 1;
    return s->write("Heap trim OK\n",13)==13 ? 0 : 1;
  }
  if(option(argc,argv,"--resize")) {
    unsigned char *a,*b,*c; void *top;
    if(cp32_heap_init(s)) return 1;
    a=cp32_malloc(32); b=cp32_malloc(96); c=cp32_malloc(32);
    if(!a || !b || !c) return 1;
    for(i=0;i<32;i++) {a[i]=(unsigned char)i;c[i]=77;}
    top=s->sbrk(0); cp32_free(b);
    if(cp32_realloc(a,112)!=a || s->sbrk(0)!=top) return 1;
    for(i=0;i<32;i++) if(a[i]!=(unsigned char)i || c[i]!=77) return 1;
    if(cp32_realloc(a,16)!=a) return 1;
    b=cp32_malloc(64);
    if(!b || s->sbrk(0)!=top) return 1;
    cp32_free(a);cp32_free(b);cp32_free(c);
    return s->write("Resize in place OK\n",sizeof("Resize in place OK\n")-1)==
      (int)(sizeof("Resize in place OK\n")-1) ? 0 : 1;
  }
  if(option(argc,argv,"--alloc")) {
    unsigned char *a,*b;
    if(cp32_heap_init(s)) return 1;
    a=cp32_calloc(4,8);
    if(!a) return 1;
    for(i=0;i<32;i++) { if(a[i]) return 1; a[i]=(unsigned char)(i+1); }
    b=cp32_realloc(a,96);
    if(!b) return 1;
    for(i=0;i<32;i++) if(b[i]!=(unsigned char)(i+1)) return 1;
    if(cp32_realloc(b,0xffffffffU) || cp32_calloc(0x80000000U,2)) return 1;
    for(i=0;i<32;i++) if(b[i]!=(unsigned char)(i+1)) return 1;
    if(cp32_realloc(b,16)!=b || cp32_realloc(b,0)) return 1;
    a=cp32_calloc(4,8);
    if(!a) return 1;
    for(i=0;i<32;i++) if(a[i]) return 1;
    cp32_free(a);
    return s->write("Calloc/realloc OK\n",sizeof("Calloc/realloc OK\n")-1)==(int)(sizeof("Calloc/realloc OK\n")-1) ? 0 : 1;
  }
  if(option(argc,argv,"--malloc")) {
    unsigned char *a,*b,*c;
    if(cp32_heap_init(s)) return 1;
    a=cp32_malloc(32); b=cp32_malloc(48);
    if(!a || !b || a==b) return 1;
    for(i=0;i<32;i++) a[i]=(unsigned char)i;
    for(i=0;i<48;i++) b[i]=(unsigned char)(i+32);
    cp32_free(a); c=cp32_malloc(16);
    if(c!=a) return 1;
    for(i=0;i<48;i++) if(b[i]!=(unsigned char)(i+32)) return 1;
    cp32_free(c); cp32_free(b);
    c=cp32_malloc(80);
    if(c!=a || cp32_malloc(0xffffffffU)) return 1;
    cp32_free(c);
    return s->write("Malloc/free OK\n",15)==15 ? 0 : 1;
  }
  if(option(argc,argv,"--heap")) {
    unsigned char *base;
    if(s->version<4 || !s->sbrk) return 1;
    base=s->sbrk(0);
    if(base==(void *)-1 || s->sbrk(32)!=base) return 1;
    for(i=0;i<32;i++) { if(base[i]) return 1; base[i]=(unsigned char)(i+1); }
    if(s->sbrk(0)!=base+32 || s->sbrk(0x7fffffff)!=(void *)-1 ||
       s->sbrk(0)!=base+32 || s->sbrk(-32)!=base+32 ||
       s->sbrk(-1)!=(void *)-1 || s->sbrk(32)!=base) return 1;
    for(i=0;i<32;i++) if(base[i]) return 1;
    if(s->sbrk(-32)!=base+32) return 1;
    return s->write("Heap grow/shrink OK\n",20)==20 ? 0 : 1;
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
