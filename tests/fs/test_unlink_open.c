#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fproc.h"
#include "abi.h"
static unsigned char disk[65536],original[65536];
static unsigned writes,fail,commit;
static int rd(unsigned at,char *p,int n){assert(at+(unsigned)n<=sizeof(disk));memcpy(p,disk+at,n);return n;}
static int wr(unsigned at,const char *p,int n){assert(at+(unsigned)n<=sizeof(disk));writes++;if(writes!=fail || commit)memcpy(disk+at,p,n);return writes==fail ? -1:n;}
static void reset(void){memcpy(disk,original,sizeof(disk));writes=fail=commit=0;cp32_cache_invalidate();}
static int open_new(void){int fd=cp32_fd_open_mode(rd,wr,sizeof(disk),"/gone",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);assert(fd>=0);assert(cp32_fd_write(fd,"old",3)==3);return fd;}
static int allocated(unsigned inode){return !!(disk[2048+inode/8]&(1U<<(inode%8)));}
int main(int argc,char **argv)
{
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(original,1,sizeof(original),f)==sizeof(original));fclose(f);
 struct cp32_minix_stat st,next;char bytes[8];int fd,alias,other,newfd;unsigned ino,steps;
 reset();fd=open_new();assert(!cp32_fd_fstat(fd,&st));ino=st.inode;
 alias=cp32_fd_dup(fd);other=cp32_fd_open(rd,sizeof(disk),"/gone");assert(alias>=0 && other>=0);
 assert(!cp32_minix_unlink(rd,wr,sizeof(disk),"/gone"));
 assert(cp32_fd_open(rd,sizeof(disk),"/gone")==-CP32_FILE_NOT_FOUND);
 assert(!cp32_fd_fstat(fd,&st) && !st.links && st.size==3 && allocated(ino));
 newfd=cp32_fd_open_mode(rd,wr,sizeof(disk),"/gone",CP32_O_CREAT|CP32_O_RDWR,0600);assert(newfd>=0);
 assert(!cp32_fd_fstat(newfd,&next) && next.inode!=ino);assert(cp32_fd_write(newfd,"new",3)==3);
 assert(cp32_fd_write(alias,"!",1)==1);assert(!cp32_fd_seek(other,0,0,0));
 assert(cp32_fd_read(other,bytes,4)==4 && !memcmp(bytes,"old!",4));
 assert(!cp32_fd_close(fd) && allocated(ino));assert(!cp32_fd_close(alias) && allocated(ino));
 assert(!cp32_fd_close(other) && !allocated(ino));
 assert(!cp32_fd_seek(newfd,0,0,0));assert(cp32_fd_read(newfd,bytes,3)==3 && !memcmp(bytes,"new",3));assert(!cp32_fd_close(newfd));
 /* Growth after unlink includes new indirect allocations in final reclamation. */
 reset();fd=open_new();assert(!cp32_fd_fstat(fd,&st));ino=st.inode;
 assert(!cp32_minix_unlink(rd,wr,sizeof(disk),"/gone"));
 assert(!cp32_fd_seek(fd,7*1024,0,0));assert(cp32_fd_write(fd,"tail",4)==4);
 assert(!cp32_fd_close(fd) && !allocated(ino));
 assert(!memcmp(disk+2048,original+2048,2048));
 /* Final close errors consume fd; every failure leaks safely, never touches a
  * new file occupying the old pathname. Test failures before/after writes. */
 reset();fd=open_new();assert(!cp32_minix_unlink(rd,wr,sizeof(disk),"/gone"));writes=0;assert(!cp32_fd_close(fd));steps=writes;
 for(unsigned after=0;after<2;after++)for(unsigned stop=1;stop<=steps;stop++) {
  reset();fd=open_new();assert(!cp32_minix_unlink(rd,wr,sizeof(disk),"/gone"));
  writes=0;fail=stop;commit=after;assert(cp32_fd_close(fd)==-CP32_SUPER_IO);
  fail=0;assert(cp32_fd_close(fd)==-CP32_FILE_BAD_FD);
  assert(cp32_fd_open(rd,sizeof(disk),"/gone")==-CP32_FILE_NOT_FOUND);
 }
 /* Unlink failure can remove the name without updating links. It must not
  * reclaim data that still has a positive link count. */
 for(unsigned after=0;after<2;after++)for(unsigned stop=1;stop<=2;stop++) {
  reset();fd=open_new();assert(!cp32_fd_fstat(fd,&st));ino=st.inode;
  writes=0;fail=stop;commit=after;assert(cp32_minix_unlink(rd,wr,sizeof(disk),"/gone")==-CP32_SUPER_IO);
  fail=0;assert(!cp32_fd_fstat(fd,&st));unsigned linked=st.links;
  assert(!cp32_fd_close(fd));assert(allocated(ino)==!!linked);
 }
 /* Process close-on-exec and exit use exactly the same final-close lifetime. */
 reset();struct cp32_fproc p={.pid=1,.root_inode=1,.cwd="/"},child={0};
 fd=open_new();p.fds[0]=fd+1;assert(!cp32_fproc_clone(&child,&p,2));
 assert(!cp32_fd_fstat(fd,&st));ino=st.inode;
 assert(!cp32_fproc_file(&p,rd,wr,sizeof(disk),CP32_APP_UNLINK,0,"/gone",6));
 p.cloexec=1;cp32_fproc_exec(&p);assert(!p.fds[0] && allocated(ino));
 cp32_fproc_exit(&child);assert(!allocated(ino));cp32_fproc_exit(&p);
 puts("open unlink: name reuse, dup/separate opens, zero links, indirect growth, final close, faults and process lifecycle pass");
}
