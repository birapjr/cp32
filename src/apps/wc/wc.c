/* MINIX wc-style file operands, selected counters and aggregate totals. */
#include "../lib/stream.h"
#include "../lib/output.h"
static int number(unsigned n,struct cp32_output *out)
{
  char text[10];unsigned pos=sizeof(text);
  do {text[--pos]='0'+n%10;n/=10;} while(n);
  while(pos<sizeof(text))if(cp32_putc(text[pos++],out)<0)return -1;
  return 0;
}
static int print_counts(unsigned *counts,unsigned flags,const char *name,struct cp32_output *out)
{
  unsigned i;int printed=0;
  for(i=0;i<3;i++)if(flags&(1U<<i)) {
    if(printed && cp32_putc(' ',out)<0)return -1;
    if(number(counts[i],out))return -1;
    printed=1;
  }
  if(name) {
    if(cp32_putc(' ',out)<0)return -1;
    while(*name)if(cp32_putc(*name++,out)<0)return -1;
  }
  if(cp32_putc('\n',out)<0)return -1;
  return cp32_flush(out);
}
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned flags=0,i=1,j,first,totals[3]={0,0,0};
  int failed=0;
  struct cp32_input input;
  struct cp32_output output;
  (void)envp;
  if(cp32_io_init(s) || cp32_output_init(&output,1))return 1;
  while(i<argc && argv[i][0]=='-' && argv[i][1]) {
    if(argv[i][1]=='-' && !argv[i][2]) {i++;break;}
    for(j=1;argv[i][j];j++) {
      if(argv[i][j]=='l')flags|=1;
      else if(argv[i][j]=='w')flags|=2;
      else if(argv[i][j]=='c')flags|=4;
      else {
        static const char usage[]="Usage: run /boot/wc [-lwc] [file ...]\n";
        cp32_write(2,usage,sizeof(usage)-1);return 2;
      }
    }
    i++;
  }
  if(!flags)flags=7;
  first=i;
  do {
    const char *name=i<argc ? argv[i] : 0;
    unsigned counts[3]={0,0,0};
    int n,inword=0,fd=!name || (name[0]=='-' && !name[1]) ? 0 : cp32_open(name);
    if(fd<0) {cp32_write(2,"wc: cannot open file\n",21);failed=1;continue;}
    cp32_input_fd(&input,fd);
    while((n=cp32_getc(&input))!=CP32_EOF) {
      int space=n==' ' || (n>='\t' && n<='\r');
      if(counts[2]==~0U) {failed=1;break;}
      counts[2]++;
      if(n=='\n')counts[0]++;
      if(!space && !inword)counts[1]++;
      inword=!space;
    }
    if(cp32_ferror(&input))failed=1;
    if(fd && cp32_close(fd))failed=1;
    for(j=0;j<3;j++) {
      if(totals[j]>~0U-counts[j])return 1;
      totals[j]+=counts[j];
    }
    if(print_counts(counts,flags,name,&output))return 1;
  } while(++i<argc);
  if(argc>first+1 && print_counts(totals,flags,"total",&output))return 1;
  return failed;
}
