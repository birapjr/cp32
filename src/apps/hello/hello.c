#include "abi.h"
static const char greeting[]="Hello from a CP32 application!\n";
/* Keep BSS in the fixture so the eventual loader must zero it. */
static volatile unsigned started;
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  (void)argc; (void)argv; (void)envp;
  if(!s || s->version!=1 || !s->write || !s->exit || started) return 1;
  started=1;
  return s->write(greeting,sizeof(greeting)-1)==(int)(sizeof(greeting)-1) ? 0 : 1;
}
