/* A second independent call0 executable using the shared bootstrap ABI.
 * Print arguments literally, separated by spaces; no options/escapes yet. */
#include "../hello/abi.h"
static volatile unsigned started;
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;
  (void)envp;
  if(!s || (s->version<1 || s->version>8) || !s->write || !s->exit || started) return 1;
  started=1;
  for(i=1;i<argc;i++) {
    if(i>1 && s->write(" ",1)!=1) return 1;
    for(n=0;argv[i][n];n++) {}
    if(s->write(argv[i],n)!=(int)n) return 1;
  }
  return s->write("\n",1)==1 ? 0 : 1;
}
