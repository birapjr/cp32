/* Replace this application; the shell continues waiting for the same PID. */
#include "../lib/io.h"
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  (void)envp;
  if(cp32_io_init(s))return 1;
  if(argc<2) {
    static const char usage[]="Usage: run /boot/exec path [args ...]\n";
    cp32_write(2,usage,sizeof(usage)-1);return 2;
  }
  cp32_execv(argv[1],(const char *const *)(argv+1));
  cp32_write(2,"exec: cannot execute\n",20);return 1;
}
