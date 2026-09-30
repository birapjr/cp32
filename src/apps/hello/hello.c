#include "abi.h"
#include "../lib/heap.h"
#include "../lib/io.h"
static const char greeting[]="Hello from a CP32 application!\n";
/* Keep BSS in the fixture so the eventual loader must zero it. */
static volatile unsigned started;
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;

  if(!s || (s->version<1 || s->version>5) || !s->write || !s->exit || started) return 1;
  started=1;
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='l' && argv[1][3]=='i' && argv[1][4]=='n' &&
     argv[1][5]=='e' && !argv[1][6]) {
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
     argv[1][2]=='i' && argv[1][3]=='o' && !argv[1][4]) {
    char line[64];int count;
    if(cp32_io_init(s) || cp32_write(1,"Standard input: ",16)!=16) return 1;
    count=cp32_read(0,line,sizeof(line));
    if(count<0 || cp32_write(2,"Read: ",6)!=6 ||
       cp32_write(1,line,(unsigned)count)!=count) return 1;
    if(cp32_read(1,line,1)!=-1 || cp32_write(0,line,1)!=-1) return 1;
    return 0;
  }
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='r' && argv[1][3]=='e' && argv[1][4]=='a' &&
     argv[1][5]=='d' && !argv[1][6]) {
    char line[64];int count;
    if(s->version<5 || !s->read) return 1;
    if(s->write("Type a line: ",13)!=13) return 1;
    count=s->read(line,sizeof(line));
    if(count<0 || count>(int)sizeof(line)) return 1;
    if(s->write("Read: ",6)!=6 || s->write(line,(unsigned)count)!=count) return 1;
    return 0;
  }
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='t' && argv[1][3]=='r' && argv[1][4]=='i' &&
     argv[1][5]=='m' && !argv[1][6]) {
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
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='r' && argv[1][3]=='e' && argv[1][4]=='s' &&
     argv[1][5]=='i' && argv[1][6]=='z' && argv[1][7]=='e' && !argv[1][8]) {
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
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='a' && argv[1][3]=='l' && argv[1][4]=='l' &&
     argv[1][5]=='o' && argv[1][6]=='c' && !argv[1][7]) {
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
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='m' && argv[1][3]=='a' && argv[1][4]=='l' &&
     argv[1][5]=='l' && argv[1][6]=='o' && argv[1][7]=='c' && !argv[1][8]) {
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
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='h' && argv[1][3]=='e' && argv[1][4]=='a' &&
     argv[1][5]=='p' && !argv[1][6]) {
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
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='e' && argv[1][3]=='n' && argv[1][4]=='v' &&
     argv[1][5]==0) {
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
