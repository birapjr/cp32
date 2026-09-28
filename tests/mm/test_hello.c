#include <assert.h>
#include <string.h>
#include "abi.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static unsigned writes;
static int output(const char *s,unsigned n) {
  const char *want="Hello from a CP32 application!\n";
  assert(n==strlen(want) && !memcmp(s,want,n)); writes++; return n;
}
static void quit(int status) { (void)status; assert(0); }
int main(void) {
  struct cp32_app_services services={1,output,quit};
  assert(app_main(0,0,0,0)==1);
  services.version=2; assert(app_main(0,0,0,&services)==1);
  services.version=1; assert(!app_main(0,0,0,&services)); assert(writes==1);
  assert(app_main(0,0,0,&services)==1); /* loader must reset BSS each launch */
}
