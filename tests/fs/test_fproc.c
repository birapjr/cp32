#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fproc.h"
#include "abi.h"
static unsigned char disk[65536];
static int rd(unsigned off,char *p,int n){assert(off+(unsigned)n<=sizeof(disk));memcpy(p,disk+off,n);return n;}
static int wr(unsigned off,const char *p,int n){assert(off+(unsigned)n<=sizeof(disk));memcpy(disk+off,p,n);return n;}
static int call(struct cp32_fproc *p,unsigned op,int fd,void *b,unsigned n)
{return cp32_fproc_file(p,rd,wr,sizeof(disk),op,fd,b,n);}
int main(int argc,char **argv)
{
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(disk,1,sizeof(disk),f)==sizeof(disk));fclose(f);
 struct cp32_fproc parent={.pid=1,.root_inode=1,.cwd="/",.mask=0022,.uid=7,.euid=8,.gid=9,.egid=10},child={0},other={0};
 struct cp32_app_open_request q={"/state",CP32_O_CREAT|CP32_O_RDWR|CP32_O_EXCL,0666};
 struct cp32_app_stat st;char buf[256];
 assert(call(&parent,CP32_APP_OPEN_MODE,0,&q,sizeof(q))==3);
 assert(call(&parent,CP32_APP_FSTAT,3,&st,sizeof(st))==0 && (st.mode&0777)==0644);
 assert(call(&parent,CP32_APP_WRITE,3,"abc",3)==3);
 assert(!call(&parent,CP32_APP_SETFD,3,0,1));
 parent.input=cp32_fd_open(rd,sizeof(disk),"/readme")+1;assert(parent.input);
 assert(!cp32_fproc_clone(&child,&parent,2));
 assert(child.pid==2 && child.uid==7 && child.euid==8 && child.gid==9 && child.egid==10 && child.root_inode==1);
 assert(call(&child,CP32_APP_GETFD,3,0,0)==1);
 assert(!call(&child,CP32_APP_SETFD,3,0,0));
 assert(call(&parent,CP32_APP_GETFD,3,0,0)==1); /* local flags */
 assert(call(&child,CP32_APP_WRITE,3,"d",1)==1);
 assert(call(&parent,CP32_APP_SEEK_CUR,3,0,0)==4); /* shared offset */
 assert(!call(&child,CP32_APP_SETFL,3,0,CP32_O_APPEND));
 assert(call(&parent,CP32_APP_GETFL,3,0,0)==(CP32_O_RDWR|CP32_O_APPEND));
 assert(call(&child,CP32_APP_DUP,3,0,0)==4 && call(&child,CP32_APP_GETFD,4,0,0)==0);
 assert(!call(&child,CP32_APP_CHDIR,0,"/boot",6));
 assert(!strcmp(parent.cwd,"/") && !strcmp(child.cwd,"/boot"));
 assert(call(&child,CP32_APP_GETCWD,0,buf,2)==-34);
 assert(!call(&child,CP32_APP_GETCWD,0,buf,sizeof(buf)) && !strcmp(buf,"/boot"));
 assert(call(&child,CP32_APP_CHDIR,0,"",1)==-2 && !strcmp(child.cwd,"/boot"));
 assert(call(&child,CP32_APP_CHDIR,0,"/readme",8)==-20 && !strcmp(child.cwd,"/boot"));
 assert(call(&child,CP32_APP_UMASK,0,0,0077)==0022 && parent.mask==0022);
 assert(!call(&child,CP32_APP_SETFD,4,0,1));cp32_fproc_exec(&child);
 assert(call(&child,CP32_APP_READ,4,buf,1)==-9);
 assert(call(&child,CP32_APP_SEEK_CUR,3,0,0)==4 && child.mask==0077 && !strcmp(child.cwd,"/boot"));
 /* Inherited stdin is a shared description; teardown closes only own refs. */
 assert(cp32_fd_read(child.input-1,buf,2)==2);
 unsigned position;assert(!cp32_fd_seek(parent.input-1,0,1,&position) && position==2);
 cp32_fproc_exit(&child);assert(!child.pid && call(&child,CP32_APP_READ,3,buf,1)==-9);
 assert(call(&parent,CP32_APP_SEEK_CUR,3,0,0)==4);
 /* Independent opens reuse the same numeric fd without sharing offsets. */
 assert(!cp32_fproc_clone(&other,&(struct cp32_fproc){.pid=4,.root_inode=1,.mask=0022,.cwd="/"},5));
 assert(call(&other,CP32_APP_OPEN,0,"/state",7)==3);
 assert(call(&other,CP32_APP_SEEK_CUR,3,0,0)==0);
 cp32_fproc_exit(&other);
 /* Exhaustion during cloning unwinds only the child's acquired refs. */
 int held[8],count=0,fd;
 while((fd=cp32_fd_open(rd,sizeof(disk),"/readme"))>=0)held[count++]=fd;
 assert(count>1);cp32_fd_close(held[--count]);
 assert(cp32_fproc_clone(&child,&parent,6)==-CP32_FILE_LIMIT && !child.pid);
 assert(call(&parent,CP32_APP_SEEK_CUR,3,0,0)==4);
 while(count)cp32_fd_close(held[--count]);
 assert(!cp32_fproc_clone(&child,&parent,7));cp32_fproc_exit(&child);
 cp32_fproc_exit(&parent);
 parent=(struct cp32_fproc){.pid=1,.root_inode=1,.cwd="/"};
 assert(call(&parent,CP32_APP_OPEN,0,"/readme",8)==3);
 count=0;while((fd=cp32_fd_open(rd,sizeof(disk),"/readme"))>=0)held[count++]=fd;
 assert(count==7 && call(&parent,CP32_APP_DUP2,3,0,4)==-24 && !parent.fds[1]);
 assert(call(&parent,CP32_APP_DUPFD,3,0,4)==-24 && !parent.fds[1]);
 assert(call(&parent,CP32_APP_DUPFD,3,0,~0U)==-22);
 assert(call(&parent,CP32_APP_DUPFD,99,0,4)==-9);
 assert(call(&parent,CP32_APP_DUP2,99,0,3)==-9 && parent.fds[0]);
 assert(call(&parent,CP32_APP_DUP2,3,0,~0U)==-9);
 while(count)assert(!cp32_fd_close(held[--count]));
 assert(call(&parent,CP32_APP_DUP2,3,0,6)==6);
 assert(!call(&parent,CP32_APP_SETFD,6,0,1));
 assert(call(&parent,CP32_APP_DUP2,6,0,6)==6 && call(&parent,CP32_APP_GETFD,6,0,0)==1);
 cp32_fproc_exec(&parent);assert(!parent.fds[3] && parent.fds[0]);
 cp32_fproc_exit(&parent);
 for(unsigned i=0;i<CP32_OPEN_MAX;i++)assert(cp32_fd_open(rd,sizeof(disk),"/readme")== (int)i);
 puts("fproc: isolation, clone/rollback, cwd, mask, identity, shared offsets/flags/stdin, exec and exit pass");
}
