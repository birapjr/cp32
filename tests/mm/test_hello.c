#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "abi.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static unsigned writes;
static char captured[256];
static int output(const char *s,unsigned n) {
  assert(strlen(captured)+n<sizeof(captured));
  strncat(captured,s,n); writes++; return n;
}
static jmp_buf exited;
static int exit_status;
static void quit(int status) { exit_status=status; longjmp(exited,1); }
static int get_pid(void) {return 12345;}
static unsigned char heap[512] __attribute__((aligned(16)));
static unsigned used;
static void *heap_break(int delta) {
 unsigned old=used;
 if(delta>0 && (unsigned)delta>sizeof(heap)-used) return (void *)-1;
 if(delta<0 && (unsigned)(-(delta+1))+1>used) return (void *)-1;
 used+=delta;
 if(used>old) memset(heap+old,0,used-old);
 return heap+old;
}
static int input(char *p,unsigned n) {assert(n==64);memcpy(p,"test\n",5);return 5;}
static unsigned line_at;
static int line_input(char *p,unsigned n) {static const char text[]="a longer line than sixteen\n";assert(n==1);if(!text[line_at])return 0;*p=text[line_at++];return 1;}
int main(int argc,char **argv) {
  struct cp32_app_services services={1,output,quit,0,0,0,0};
  if(argc>1 && !strcmp(argv[1],"line")) {
    char *args[]={"hello","--line",0};services.version=5;services.read=line_input;
    assert(!app_main(2,args,0,&services));assert(!strcmp(captured,"Line: a longer line than sixteen\n"));return 0;
  }
  if(argc>1 && !strcmp(argv[1],"io")) {
    char *args[]={"hello","--io",0};services.version=5;services.read=input;
    assert(!app_main(2,args,0,&services));assert(!strcmp(captured,"Standard input: Read: test\n"));return 0;
  }
  if(argc>1 && !strcmp(argv[1],"read")) {
    char *args[]={"hello","--read",0};services.version=5;services.read=input;
    assert(!app_main(2,args,0,&services));assert(!strcmp(captured,"Type a line: Read: test\n"));return 0;
  }
  if(argc>1 && !strcmp(argv[1],"trim")) {
    char *args[]={"hello","--trim",0};
    services.version=4;services.sbrk=heap_break;
    assert(!app_main(2,args,0,&services));assert(!strcmp(captured,"Heap trim OK\n"));
    return 0;
  }
  if(argc>1 && !strcmp(argv[1],"resize")) {
    char *args[]={"hello","--resize",0};
    services.version=4;services.sbrk=heap_break;
    assert(!app_main(2,args,0,&services));
    assert(!strcmp(captured,"Resize in place OK\n"));
    return 0;
  }
  if(argc>1 && !strcmp(argv[1],"alloc")) {
    char *args[]={"hello","--alloc",0};
    services.version=4;services.sbrk=heap_break;
    assert(!app_main(2,args,0,&services));
    assert(!strcmp(captured,"Calloc/realloc OK\n"));
    return 0;
  }
  if(argc>1 && !strcmp(argv[1],"malloc")) {
    char *args[]={"hello","--malloc",0};
    services.version=4;services.sbrk=heap_break;
    assert(!app_main(2,args,0,&services));
    assert(!strcmp(captured,"Malloc/free OK\n"));
    return 0;
  }
  if(argc>1 && !strcmp(argv[1],"heap")) {
    char *args[]={"hello","--heap",0};
    services.version=4;services.sbrk=heap_break;
    assert(!app_main(2,args,0,&services));
    assert(!strcmp(captured,"Heap grow/shrink OK\n") && !used);
    return 0;
  }
  if(argc>1 && !strcmp(argv[1],"ppid")) {
    char *args[]={"hello","--ppid",0};
    services.version=3;services.getppid=get_pid;
    assert(!app_main(2,args,0,&services));assert(!strcmp(captured,"PPID=12345\n"));
    return 0;
  }
  if(argc>1 && !strcmp(argv[1],"pid")) {
    char *args[]={"hello","--pid",0};
    services.version=2;services.getpid=get_pid;
    assert(!app_main(2,args,0,&services));assert(!strcmp(captured,"PID=12345\n"));
    return 0;
  }
  if(argc>1 && !strcmp(argv[1],"env")) {
    char *args[]={"hello","--env",0};
    char *env[]={"HOME=/","PATH=/boot","USER=root",0};
    assert(!app_main(2,args,env,&services));
    assert(!strcmp(captured,"HOME=/\nPATH=/boot\nUSER=root\n"));
    return 0;
  }
  if(argc>1 && !strcmp(argv[1],"empty-env")) {
    char *args[]={"hello","--env",0};
    char *env[]={0};
    assert(!app_main(2,args,env,&services) && !writes);
    return 0;
  }
  if(argc>1) {
    char *args[]={"hello","--exit",argv[1],0};
    if(argc>2) {
      assert(app_main(3,args,0,&services)==2);
      assert(!strcmp(captured,"Usage: hello --exit 0..65535\n"));
    } else {
      if(!setjmp(exited)) { app_main(3,args,0,&services); assert(0); }
      assert(exit_status==atoi(argv[1]) && !writes);
    }
    return 0;
  }
  assert(app_main(0,0,0,0)==1);
  services.version=6; assert(app_main(0,0,0,&services)==1);
  char *args[]={"hello","one","two",0};
  services.version=1; assert(!app_main(3,args,0,&services)); assert(writes==7);
  assert(!strcmp(captured,"Hello from a CP32 application!\narg: one\narg: two\n"));
  assert(app_main(0,0,0,&services)==1); /* loader must reset BSS each launch */
}
