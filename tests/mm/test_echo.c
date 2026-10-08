#include <assert.h>
#include <string.h>
#include "abi.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static char output[256];
static int fail;
static int write_text(const char *s,unsigned n) {
  if(fail) return -1;
  assert(strlen(output)+n<sizeof(output)); strncat(output,s,n);return n;
}
static void quit(int n) {(void)n;assert(0);}
int main(int argc,char **argv) {
  struct cp32_app_services services={5,write_text,quit,0,0,0,0,0};
  char *args[]={"echo","one","two",0};
  if(argc>1 && !strcmp(argv[1],"empty")) {
    assert(!app_main(1,args,0,&services));assert(!strcmp(output,"\n"));
  } else if(argc>1 && !strcmp(argv[1],"failure")) {
    fail=1;assert(app_main(3,args,0,&services)==1);
  } else {
    assert(app_main(3,args,0,0)==1);
    assert(!app_main(3,args,0,&services));assert(!strcmp(output,"one two\n"));
    assert(app_main(3,args,0,&services)==1);
  }
}
