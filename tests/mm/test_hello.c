#include <assert.h>
#include <string.h>
#include "abi.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static unsigned writes;
static char captured[256];
static int output(const char *s,unsigned n) {
  assert(strlen(captured)+n<sizeof(captured));
  strncat(captured,s,n); writes++; return n;
}
static void quit(int status) { (void)status; assert(0); }
int main(void) {
  struct cp32_app_services services={1,output,quit};
  assert(app_main(0,0,0,0)==1);
  services.version=2; assert(app_main(0,0,0,&services)==1);
  char *args[]={"hello","one","two",0};
  services.version=1; assert(!app_main(3,args,0,&services)); assert(writes==7);
  assert(!strcmp(captured,"Hello from a CP32 application!\narg: one\narg: two\n"));
  assert(app_main(0,0,0,&services)==1); /* loader must reset BSS each launch */
}
