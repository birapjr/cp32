/* Bounded, streaming MINIX-style ls: disk order, no recursion or name sorting. */
#include "../lib/io.h"
#include "../lib/output.h"
static int text(struct cp32_output *out,const char *s)
{
  while(*s)if(cp32_putc(*s++,out)<0)return -1;
  return 0;
}
static int number(struct cp32_output *out,unsigned n)
{
  char digits[10];unsigned i=10;
  do {digits[--i]='0'+n%10;n/=10;}while(n);
  while(i<10)if(cp32_putc(digits[i++],out)<0)return -1;
  return cp32_putc(' ',out)<0 ? -1 : 0;
}
static int show(struct cp32_output *out,const char *name,const struct cp32_app_stat *s,int flags)
{
  if(flags&2) {
    const char *rwx="rwx";unsigned i;
    if(cp32_putc((s->mode & 0170000)==0040000 ? 'd':'-',out)<0)return -1;
    for(i=0;i<9;i++) {
      int c=(s->mode & (0400U>>i)) ? rwx[i%3]:'-';
      if(i==2 && (s->mode & 04000))c=c=='x' ? 's':'S';
      if(i==5 && (s->mode & 02000))c=c=='x' ? 's':'S';
      if(i==8 && (s->mode & 01000))c=c=='x' ? 't':'T';
      if(cp32_putc(c,out)<0)return -1;
    }
    if(cp32_putc(' ',out)<0 || number(out,s->links) || number(out,s->uid) ||
       number(out,s->gid) || number(out,s->size))return -1;
  }
  if(text(out,name) || cp32_putc('\n',out)<0)return -1;
  return cp32_flush(out);
}
static int list(struct cp32_output *out,const char *path,int flags)
{
  struct cp32_app_stat info;
  struct cp32_app_dirent entry;
  int fd,r,failed=0;
  if(cp32_stat(path,&info))return -1;
  if((info.mode & 0170000)!=0040000 || (flags&4))return show(out,path,&info,flags);
  fd=cp32_opendir(path);
  if(fd<0)return -1;
  /* Descriptor metadata follows the opened inode independently of its offset. */
  if(cp32_fstat(fd,&info) || (info.mode & 0170000)!=0040000) {
    cp32_close(fd);return -1;
  }
  while((r=cp32_readdir(fd,&entry))>0) {
    if(!(flags&1) && entry.name[0]=='.')continue;
    if(flags&2) {
      char full[256];unsigned i=0,j=0;
      while(path[i] && i<255) {full[i]=path[i];i++;}
      if(i && full[i-1]!='/' && i<255)full[i++]='/';
      while(entry.name[j] && i<255)full[i++]=entry.name[j++];
      full[i]=0;
      if(entry.name[j] || cp32_stat(full,&info)) {failed=1;break;}
    }
    if(show(out,entry.name,&info,flags)) {failed=1;break;}
  }
  if(r<0)failed=1;
  if(cp32_close(fd))failed=1;
  return failed ? -1:0;
}
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *api)
{
  struct cp32_output out;unsigned i=1,j,first;int flags=0,failed=0;
  (void)envp;
  if(cp32_io_init(api) || cp32_output_init(&out,1))return 1;
  while(i<argc && argv[i][0]=='-' && argv[i][1]) {
    if(argv[i][1]=='-' && !argv[i][2]) {i++;break;}
    for(j=1;argv[i][j];j++) {
      if(argv[i][j]=='a')flags|=1;
      else if(argv[i][j]=='l')flags|=2;
      else if(argv[i][j]=='d')flags|=4;
      else {
        static const char usage[]="Usage: run /boot/ls [-ald] [path ...]\n";
        cp32_write(2,usage,sizeof(usage)-1);return 2;
      }
    }
    i++;
  }
  first=i;
  do {
    const char *path=i<argc ? argv[i]:".";
    if(argc>first+1 && (text(&out,path) || text(&out,":\n") || cp32_flush(&out)))return 1;
    if(list(&out,path,flags)) {
      static const char error[]="ls: cannot list path\n";
      cp32_write(2,error,sizeof(error)-1);failed=1;
    }
  }while(++i<argc);
  return failed;
}
