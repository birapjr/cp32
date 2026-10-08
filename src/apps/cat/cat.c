/* Sequential regular-file operands or stdin, over application-owned handles. */
#include "../lib/io.h"
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  char buffer[64];
  unsigned i=1;
  int failed=0;
  (void)envp;
  if(cp32_io_init(s)) return 1;
  do {
    int fd=argc==1 || (argv[i][0]=='-' && !argv[i][1]) ? 0 : cp32_open(argv[i]);
    int n=0;
    if(fd<0) {cp32_write(2,"cat: cannot open file\n",22);failed=1;}
    else {
      while((n=cp32_read(fd,buffer,sizeof(buffer)))>0)
        if(cp32_write(1,buffer,(unsigned)n)!=n) {failed=1;break;}
      if(n<0)failed=1;
      if(fd && cp32_close(fd))failed=1;
    }
  } while(++i<argc);
  return failed;
}
