#include <assert.h>
#include <string.h>
#include "io.h"
static char source[600],result[600];
static unsigned done,calls;
static int mode;
static const char *line;static unsigned at;static int fail_at=-1;
static int output(const char *p,unsigned n) {
 assert(n<=256 && p==source+done);calls++;
 if(mode==1 && done>=100) return -1;
 if(mode==2) return 0;
 if(mode==3) return n+1;
 if(n>100) n=100;
 memcpy(result+done,p,n);done+=n;return n;
}
static int input(char *p,unsigned n) {
 if(line) {assert(n==1);if((int)at==fail_at)return -1; if(!line[at])return 0; *p=line[at++];return 1;}
 assert(n==64); if(mode==1)return -1;if(mode==2)return 0;if(mode==3)return 65;
 memcpy(p,"ok\n",3);return 3;
}
static int file_output(unsigned op,int fd,void *p,unsigned n) {
 assert(op==CP32_APP_WRITE && fd==3 && n<=64);calls++;
 if(!n)return -CP32_EBADF;
 assert(p==source+done);
 if(mode==1 && done>=100)return -CP32_ENOSPC;
 if(mode==2)return -CP32_EROFS;
 if(n>20)n=20;
 memcpy(result+done,p,n);done+=n;return n;
}
static int mkdir_call(unsigned op,int fd,void *p,unsigned n) {
 struct cp32_app_open_request *q=p;
 assert(op==CP32_APP_MKDIR && !fd && n==sizeof(*q));
 assert(!strcmp(q->path,"dir") && !q->flags && q->mode==0777);
 return -CP32_EEXIST;
}
static int link_call(unsigned op,int fd,void *p,unsigned n) {
 struct cp32_app_link_request *q=p;
 assert(op==CP32_APP_LINK && !fd && n==sizeof(*q));
 assert(!strcmp(q->source,"old") && !strcmp(q->destination,"new"));
 return -CP32_EEXIST;
}
static int chmod_call(unsigned op,int fd,void *p,unsigned n) {
 struct cp32_app_open_request *q=p;
 assert(op==CP32_APP_CHMOD && !fd && n==sizeof(*q));
 assert(!strcmp(q->path,"file") && !q->flags && q->mode==0640);
 return -CP32_ENOENT;
}
static int rmdir_call(unsigned op,int fd,void *p,unsigned n) {
 assert(op==CP32_APP_RMDIR && !fd && n==4 && !strcmp(p,"dir"));
 return -CP32_ENOTEMPTY;
}
static int rename_call(unsigned op,int fd,void *p,unsigned n) {
 struct cp32_app_link_request *q=p;
 assert(op==CP32_APP_RENAME_NOREPLACE && !fd && n==sizeof(*q));
 assert(!strcmp(q->source,"old") && !strcmp(q->destination,"new"));
 return -CP32_EEXIST;
}
static int truncate_call(unsigned op,int fd,void *p,unsigned n) {
 assert(op==CP32_APP_FTRUNCATE && fd==3 && !p && n==1024);
 return -CP32_EFBIG;
}
static int dup2_call(unsigned op,int fd,void *p,unsigned n) {
 assert(op==CP32_APP_DUP2 && fd==3 && !p && n==6);return 6;
}
static int dupfd_call(unsigned op,int fd,void *p,unsigned n) {
 assert(op==CP32_APP_DUPFD && fd==3 && !p && n==5);return 5;
}
static int fchmod_call(unsigned op,int fd,void *p,unsigned n) {
 assert(op==CP32_APP_FCHMOD && fd==3 && !p && n==0640);return 0;
}
static int position_call(unsigned op,int fd,void *p,unsigned n) {
 struct cp32_app_position_request *q=p;
 assert(fd==3 && n==sizeof(*q) && q->count<=64 && q->offset==10+done);
 if(op==CP32_APP_PREAD){assert(q->count==64);memcpy(q->data,"ok",2);return 2;}
 assert(op==CP32_APP_PWRITE);calls++;
 if(mode==1 && done>=100)return -CP32_ENOSPC;
 if(mode==2)return 0;
 unsigned part=q->count;if(part>20)part=20;
 assert(!memcmp(q->data,source+done,part));done+=part;return part;
}
static int space_call(unsigned op,int fd,void *p,unsigned n) {
 assert(op==CP32_APP_FSINFO && !fd && p && n==sizeof(struct cp32_app_fsinfo));
 return -CP32_EIO;
}
int main(void) {
 struct cp32_app_services s={5,output,0,0,0,0,input,0};char b[128];
 assert(cp32_write(1,source,1)==-1 && cp32_io_init(0)==-1);
 assert(!cp32_io_init(&s) && cp32_io_init(&s)==-1);
 memset(source,'a',sizeof(source));
 assert(cp32_write(1,source,600)==600 && calls==6 && !memcmp(source,result,600));
 calls=done=0;mode=1;assert(cp32_write(2,source,600)==100 && calls==2);
 done=0;mode=2;assert(cp32_write(1,source,600)==0);
 mode=3;assert(cp32_write(1,source,600)==-1);
 assert(cp32_write(0,source,1)==-1 && cp32_write(1,0,1)==-1);
 assert(cp32_write(1,source,~0U)==-1 && !cp32_write(1,0,0));
 mode=0;assert(cp32_read(0,b,sizeof(b))==3 && !memcmp(b,"ok\n",3));
 mode=1;assert(cp32_read(0,b,64)==-1);
 mode=2;assert(!cp32_read(0,b,64));mode=3;assert(cp32_read(0,b,64)==-1);
 assert(cp32_read(1,b,1)==-1 && cp32_read(0,0,1)==-1 && !cp32_read(0,0,0));
 line="abcdef\nnext\n";at=0;
 assert(cp32_readline(b,4)==3 && !strcmp(b,"abc"));
 assert(cp32_readline(b,8)==4 && !strcmp(b,"def\n"));
 assert(cp32_readline(b,8)==5 && !strcmp(b,"next\n"));
 assert(!cp32_readline(b,8) && !b[0]);
 line="\n";at=0;assert(cp32_readline(b,2)==1 && !strcmp(b,"\n"));
 line="partial";at=0;fail_at=3;
 assert(cp32_readline(b,16)==-1 && !strcmp(b,"par"));
 assert(cp32_readline(b,1)==-1 && cp32_readline(0,16)==-1);
 assert(cp32_read(2,b,1)==-1 && cp32_errno==CP32_EBADF);
 assert(cp32_write(1,0,1)==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_read(0,b,~0U)==-1 && cp32_errno==CP32_EINVAL);
 assert(!cp32_write(1,0,0) && cp32_errno==CP32_EINVAL);
 line=0;mode=1;
 assert(cp32_read(0,b,64)==-1 && cp32_errno==CP32_EIO);
 mode=2;assert(!cp32_read(0,b,64) && cp32_errno==CP32_EIO);
 s.version=9;s.file=file_output;done=calls=0;mode=0;
 assert(cp32_write(3,source,600)==600 && calls==30 && !memcmp(source,result,600));
 done=calls=0;mode=1;
 assert(cp32_write(3,source,600)==100 && cp32_errno==CP32_ENOSPC && calls==6);
 done=0;mode=2;assert(cp32_write(3,source,1)==-1 && cp32_errno==CP32_EROFS);
 assert(cp32_write(3,0,0)==-1 && cp32_errno==CP32_EBADF);
 assert(cp32_mkdir("dir",0777)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=12;s.file=mkdir_call;
 assert(cp32_mkdir(0,0777)==-1 && cp32_errno==CP32_EFAULT);
 memset(source,'a',256);source[256]=0;
 assert(cp32_mkdir(source,0777)==-1 && cp32_errno==CP32_ENAMETOOLONG);
 assert(cp32_mkdir("dir",0777)==-1 && cp32_errno==CP32_EEXIST);
 assert(cp32_link("old","new")==-1 && cp32_errno==CP32_ENOSYS);
 s.version=13;s.file=link_call;
 assert(cp32_link(0,"new")==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_link("old",0)==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_link(source,"new")==-1 && cp32_errno==CP32_ENAMETOOLONG);
 assert(cp32_link("old",source)==-1 && cp32_errno==CP32_ENAMETOOLONG);
 assert(cp32_link("old","new")==-1 && cp32_errno==CP32_EEXIST);
 assert(cp32_chmod("file",0640)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=14;s.file=chmod_call;
 assert(cp32_chmod(0,0640)==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_chmod(source,0640)==-1 && cp32_errno==CP32_ENAMETOOLONG);
 assert(cp32_chmod("file",01000)==-1 && cp32_errno==CP32_EINVAL);
 assert(cp32_chmod("file",0640)==-1 && cp32_errno==CP32_ENOENT);
 assert(cp32_rmdir("dir")==-1 && cp32_errno==CP32_ENOSYS);
 s.version=15;s.file=rmdir_call;
 assert(cp32_rmdir(0)==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_rmdir(source)==-1 && cp32_errno==CP32_ENAMETOOLONG);
 assert(cp32_rmdir("dir")==-1 && cp32_errno==CP32_ENOTEMPTY);
 assert(cp32_rename_noreplace("old","new")==-1 && cp32_errno==CP32_ENOSYS);
 s.version=16;s.file=rename_call;
 assert(cp32_rename_noreplace(0,"new")==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_rename_noreplace("old",0)==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_rename_noreplace(source,"new")==-1 && cp32_errno==CP32_ENAMETOOLONG);
 assert(cp32_rename_noreplace("old",source)==-1 && cp32_errno==CP32_ENAMETOOLONG);
 assert(cp32_rename_noreplace("old","new")==-1 && cp32_errno==CP32_EEXIST);
 assert(cp32_ftruncate(3,1024)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=17;s.file=truncate_call;
 assert(cp32_ftruncate(3,-1)==-1 && cp32_errno==CP32_EINVAL);
 assert(cp32_ftruncate(3,1024)==-1 && cp32_errno==CP32_EFBIG);
 assert(cp32_dup2(3,6)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=18;s.file=dup2_call;
 assert(cp32_dup2(-1,6)==-1 && cp32_errno==CP32_EBADF);
 assert(cp32_dup2(3,1)==-1 && cp32_errno==CP32_EBADF);
 assert(cp32_dup2(3,7)==-1 && cp32_errno==CP32_EBADF);
 assert(cp32_dup2(3,6)==6);
 assert(cp32_fcntl(3,CP32_F_DUPFD,5)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=19;s.file=dupfd_call;
 assert(cp32_fcntl(3,CP32_F_DUPFD,5)==5);
 assert(cp32_fchmod(3,0640)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=20;s.file=fchmod_call;
 assert(cp32_fchmod(3,01000)==-1 && cp32_errno==CP32_EINVAL);
 assert(!cp32_fchmod(3,0640));
 assert(cp32_pread(3,b,1,0)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=21;s.file=position_call;done=calls=0;mode=0;
 assert(cp32_pread(3,0,1,0)==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_pwrite(3,source,1,-1)==-1 && cp32_errno==CP32_EINVAL);
 assert(cp32_pwrite(3,source,2,0x7fffffff)==-1 && cp32_errno==CP32_EINVAL);
 assert(cp32_pread(3,b,100,10)==2 && !memcmp(b,"ok",2));
 assert(cp32_pwrite(3,source,150,10)==150 && calls==8);
 done=calls=0;mode=1;assert(cp32_pwrite(3,source,150,10)==100 && cp32_errno==CP32_ENOSPC);
 done=0;mode=2;assert(cp32_pwrite(3,source,1,10)==0);
 struct cp32_app_fsinfo space;
 assert(cp32_fsinfo(&space)==-1 && cp32_errno==CP32_ENOSYS);
 s.version=22;s.file=space_call;
 assert(cp32_fsinfo(0)==-1 && cp32_errno==CP32_EFAULT);
 assert(cp32_fsinfo(&space)==-1 && cp32_errno==CP32_EIO);
 return 0;
}
