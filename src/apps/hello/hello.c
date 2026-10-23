#include "abi.h"
#include "../lib/heap.h"
#include "../lib/io.h"
static const char greeting[]="Hello from a CP32 application!\n";
/* Keep BSS in the fixture so the eventual loader must zero it. */
static volatile unsigned started;
static __attribute__((noinline)) int option(unsigned argc,char **argv,const char *name)
{
  unsigned i=0;
  if(argc!=2) return 0;
  while(name[i] && name[i]==argv[1][i]) i++;
  return !name[i] && !argv[1][i];
}
/* Short, repeatable hardware acceptance for writable file descriptors. */
static int io_check(void)
{
  char data[64];int fd,n;unsigned at=0,j;
  struct cp32_app_stat st;
  fd=cp32_open("/wtest");if(fd<0)return 31;
  if(cp32_fstat(fd,&st) || st.size!=1026)return 32;
  while((n=cp32_read(fd,data,sizeof(data)))>0) {
    for(j=0;j<(unsigned)n;j++,at++) {
      char expected=at<4 ? "CP32"[at]:at<580 ? 'x':0;
      if(at>=510 && at<514)expected="ABCD"[at-510];
      if(at==1024)expected='Z';if(at==1025)expected='!';
      if(data[j]!=expected)return 33;
    }
  }
  if(n || at!=1026 || cp32_close(fd))return 34;
  return 0;
}
static int io_test(void)
{
  char data[64];int fd,copy,other,r;unsigned i;struct cp32_app_stat st;
  fd=cp32_open_flags("/wtest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0)return 20;
  if(cp32_write(fd,"old",3)!=3 || (copy=cp32_dup(fd))<0)return 21;
  other=cp32_open_flags("/wtest",CP32_O_WRONLY|CP32_O_TRUNC,0);
  if(other<0 || cp32_read(other,data,1)!=-1 || cp32_errno!=CP32_EBADF || cp32_close(other))return 22;
  if(cp32_fstat(fd,&st) || st.size || cp32_lseek(fd,0,0))return 23;
  if(cp32_write(copy,"CP32",4)!=4 || cp32_lseek(fd,0,1)!=4)return 24;
  for(i=0;i<64;i++)data[i]='x';
  for(i=0;i<9;i++)if(cp32_write(fd,data,64)!=64)return 25;
  if(cp32_lseek(fd,510,0)!=510 || cp32_write(fd,"ABCD",4)!=4)return 26;
  if(cp32_lseek(fd,1024,0)!=1024 || cp32_write(copy,"Z",1)!=1)return 27;
  other=cp32_open_flags("/wtest",CP32_O_WRONLY|CP32_O_APPEND,0);
  if(other<0 || cp32_lseek(other,0,0) || cp32_write(other,"!",1)!=1 || cp32_close(other))return 28;
  if(cp32_lseek(fd,0,2)!=1026 || cp32_open_flags("/wtest",CP32_O_WRONLY|CP32_O_CREAT|CP32_O_EXCL,0600)!=-1 || cp32_errno!=CP32_EEXIST)return 29;
  if(cp32_close(fd) || cp32_close(copy))return 30;
  r=io_check();if(r)return r;
  fd=cp32_open("/wtest");
  if(fd<0 || cp32_write(fd,"bad",3)!=-1 || cp32_errno!=CP32_EBADF)return 35;
  /* Intentionally leave fd open: normal exit must reclaim it. */
  return 0;
}
#if !CP32_ROOT_RAM
/* This acceptance command creates an SD file; omit it from RAM recovery's
 * embedded demo application while retaining the kernel's process-state API. */
/* Process-local state must survive exec, while CLOEXEC applies at commit. */
static int space_test(void)
{
  struct cp32_app_fsinfo a,b,c;int fd;
  if(cp32_fsinfo(&a) || a.zone_size!=1024 || a.name_max!=14 || !a.free_inodes || !a.free_zones)return 170;
  fd=cp32_open_flags("/stest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0 || cp32_fsinfo(&b) || b.free_inodes+1!=a.free_inodes || b.free_zones!=a.free_zones)return 171;
  if(cp32_write(fd,"hi",2)!=2 || cp32_fsinfo(&c) || c.free_zones+1!=a.free_zones)return 172;
  if(cp32_unlink("/stest") || cp32_fsinfo(&b) || b.free_zones!=c.free_zones || b.free_inodes!=c.free_inodes)return 173;
  if(cp32_close(fd) || cp32_fsinfo(&b) || b.free_zones!=a.free_zones || b.free_inodes!=a.free_inodes)return 174;
  return 0;
}
static int position_test(void)
{
  char data[80],b[64];int fd,alias,reader;unsigned i;
  fd=cp32_open_flags("/ptest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0 || cp32_write(fd,"hi",2)!=2 || (alias=cp32_dup(fd))<0 || (reader=cp32_open("/ptest"))<0)return 160;
  if(cp32_fcntl(fd,CP32_F_SETFL,CP32_O_APPEND) || cp32_pwrite(alias,"yo",2,0)!=2 || cp32_lseek(fd,0,1)!=2)return 161;
  if(cp32_pread(reader,b,2,0)!=2 || b[0]!='y' || b[1]!='o' || cp32_lseek(reader,0,1))return 162;
  for(i=0;i<80;i++)data[i]='A'+i%26;
  if(cp32_pwrite(fd,data,80,7150)!=80 || cp32_lseek(alias,0,1)!=2 || !(cp32_fcntl(fd,CP32_F_GETFL,0)&CP32_O_APPEND))return 163;
  if(cp32_pread(reader,b,64,2)!=64)return 164;
  for(i=0;i<64;i++)if(b[i])return 165;
  if(cp32_pread(reader,b,18,7150)!=18)return 166;
  for(i=0;i<18;i++)if(b[i]!=data[i])return 167;
  if(cp32_pread(reader,b,62,7168)!=62)return 166;
  for(i=0;i<62;i++)if(b[i]!=data[i+18])return 167;
  if(cp32_pwrite(reader,"x",1,0)!=-1 || cp32_errno!=CP32_EBADF || cp32_pread(fd,b,1,-1)!=-1 || cp32_errno!=CP32_EINVAL)return 168;
  if(cp32_unlink("/ptest") || cp32_pread(fd,b,2,0)!=2 || b[0]!='y' || b[1]!='o' || cp32_close(fd) || cp32_close(alias) || cp32_close(reader))return 169;
  return 0;
}
static int fchmod_test(void)
{
  struct cp32_app_stat a,b;int fd,reader;char data[2];
  fd=cp32_open_flags("/htest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0 || cp32_write(fd,"ok",2)!=2 || (reader=cp32_open("/htest"))<0 || cp32_fstat(fd,&a))return 150;
  cp32_umask(0777);
  if(cp32_fchmod(reader,0440) || cp32_fstat(fd,&b) || b.mode!=0100440 || b.inode!=a.inode || b.size!=2 || cp32_lseek(fd,0,1)!=2)return 151;
  if(cp32_open_flags("/htest",CP32_O_WRONLY,0)!=-1 || cp32_errno!=CP32_EACCES)return 152;
  if(cp32_unlink("/htest") || cp32_fchmod(reader,0600) || cp32_fstat(fd,&b) || b.mode!=0100600 || b.links)return 153;
  if(cp32_fchmod(reader,01000)!=-1 || cp32_errno!=CP32_EINVAL || cp32_read(reader,data,2)!=2 || data[0]!='o' || data[1]!='k')return 154;
  if(cp32_close(fd) || cp32_close(reader) || cp32_fchmod(reader,0600)!=-1 || cp32_errno!=CP32_EBADF)return 155;
  cp32_umask(0022);
  if(cp32_mkdir("/htest",0700) || (fd=cp32_opendir("/htest"))<0 || cp32_fchmod(fd,0750) || cp32_fstat(fd,&b) || b.mode!=0040750 || cp32_close(fd) || cp32_rmdir("/htest"))return 156;
  return 0;
}
static int dupfd_test(void)
{
  int fd,a,b,c;char data[2];
  fd=cp32_open_flags("/ftest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd!=3 || cp32_write(fd,"hi",2)!=2 || cp32_fcntl(fd,CP32_F_SETFD,1))return 140;
  a=cp32_fcntl(fd,CP32_F_DUPFD,5);b=cp32_fcntl(fd,CP32_F_DUPFD,5);
  if(a!=5 || b!=6 || cp32_fcntl(a,CP32_F_GETFD,0) || cp32_fcntl(fd,CP32_F_GETFD,0)!=1)return 141;
  if(cp32_fcntl(fd,CP32_F_DUPFD,5)!=-1 || cp32_errno!=CP32_EMFILE)return 142;
  c=cp32_fcntl(fd,CP32_F_DUPFD,0);if(c!=4)return 143;
  if(cp32_fcntl(fd,CP32_F_DUPFD,7)!=-1 || cp32_errno!=CP32_EINVAL)return 144;
  if(cp32_lseek(a,0,0) || cp32_read(b,data,1)!=1 || data[0]!='h' || cp32_read(c,data,1)!=1 || data[0]!='i')return 145;
  if(cp32_fcntl(a,CP32_F_SETFL,CP32_O_APPEND) || !(cp32_fcntl(fd,CP32_F_GETFL,0)&CP32_O_APPEND))return 146;
  if(cp32_close(a) || cp32_fcntl(a,CP32_F_DUPFD,0)!=-1 || cp32_errno!=CP32_EBADF || cp32_fcntl(fd,CP32_F_DUPFD,4)!=5)return 147;
  if(cp32_unlink("/ftest") || cp32_close(fd) || cp32_close(a) || cp32_close(b) || cp32_close(c))return 148;
  return 0;
}
static int dup2_test(void)
{
  struct cp32_app_stat a,b;char data[2];int src,target,alias,reader;
  src=cp32_open_flags("/qtest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  target=cp32_open_flags("/qold",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(src!=3 || target!=4 || cp32_write(src,"hi",2)!=2 || (alias=cp32_dup(src))!=5 || (reader=cp32_open("/qtest"))!=6)return 131;
  if(cp32_fcntl(src,CP32_F_SETFD,CP32_FD_CLOEXEC) || cp32_dup2(src,src)!=src || cp32_fcntl(src,CP32_F_GETFD,0)!=1)return 132;
  if(cp32_fcntl(target,CP32_F_SETFD,CP32_FD_CLOEXEC) || cp32_unlink("/qold") || cp32_dup2(src,target)!=target || cp32_fcntl(target,CP32_F_GETFD,0))return 133;
  if(cp32_fstat(src,&a) || cp32_fstat(target,&b) || a.inode!=b.inode || cp32_lseek(target,0,1)!=2)return 134;
  if(cp32_lseek(alias,0,0) || cp32_write(target,"yo",2)!=2 || cp32_read(reader,data,2)!=2 || data[0]!='y' || data[1]!='o')return 135;
  if(cp32_dup2(99,target)!=-1 || cp32_errno!=CP32_EBADF || cp32_fstat(target,&b) || b.inode!=a.inode)return 136;
  if(cp32_close(reader) || cp32_dup2(reader,target)!=-1 || cp32_errno!=CP32_EBADF || cp32_dup2(src,reader)!=reader)return 137;
  if(cp32_fcntl(target,CP32_F_SETFL,CP32_O_APPEND) || !(cp32_fcntl(src,CP32_F_GETFL,0)&CP32_O_APPEND))return 138;
  if(cp32_unlink("/qtest") || cp32_close(src) || cp32_close(target) || cp32_close(alias) || cp32_close(reader))return 139;
  return 0;
}
static int resize_test(void)
{
  struct cp32_app_stat st;char b[64];int fd,copy,reader;unsigned i;
  fd=cp32_open_flags("/ztest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0 || cp32_write(fd,"hello",5)!=5 || (copy=cp32_dup(fd))<0 || (reader=cp32_open("/ztest"))<0)return 120;
  if(cp32_ftruncate(reader,0)!=-1 || cp32_errno!=CP32_EBADF)return 121;
  if(cp32_ftruncate(copy,2) || cp32_lseek(fd,0,1)!=5 || cp32_fstat(reader,&st) || st.size!=2)return 122;
  if(cp32_ftruncate(fd,8193) || cp32_lseek(copy,0,1)!=5 || cp32_lseek(reader,0,0) || cp32_read(reader,b,64)!=64 || b[0]!='h' || b[1]!='e')return 123;
  for(i=2;i<64;i++)if(b[i])return 124;
  if(cp32_lseek(reader,8192,0)!=8192 || cp32_read(reader,b,1)!=1 || b[0])return 125;
  if(cp32_lseek(fd,8192,0)!=8192 || cp32_write(fd,"X",1)!=1 || cp32_unlink("/ztest") || cp32_ftruncate(copy,0))return 126;
  if(cp32_fstat(reader,&st) || st.size || st.links || cp32_ftruncate(fd,16) || cp32_lseek(reader,0,0) || cp32_read(reader,b,16)!=16)return 127;
  for(i=0;i<16;i++)if(b[i])return 128;
  if(cp32_ftruncate(fd,-1)!=-1 || cp32_errno!=CP32_EINVAL || cp32_ftruncate(fd,263*1024+1)!=-1 || cp32_errno!=CP32_EFBIG)return 129;
  if(cp32_close(fd) || cp32_close(copy) || cp32_close(reader))return 130;
  return 0;
}
static int rename_test(void)
{
  struct cp32_app_stat a,b;char data[2];int fd;
  if(cp32_mkdir("/ntest",0700) || cp32_chdir("/ntest"))return 110;
  fd=cp32_open_flags("a",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0 || cp32_write(fd,"hi",2)!=2 || cp32_fstat(fd,&a))return 111;
  if(cp32_rename_noreplace("a","b") || cp32_stat("a",&b)!=-1 || cp32_errno!=CP32_ENOENT)return 112;
  if(cp32_stat("b",&b) || b.inode!=a.inode || b.links!=1 || b.size!=2)return 113;
  if(cp32_rename_noreplace("b","/readme")!=-1 || cp32_errno!=CP32_EEXIST)return 114;
  if(cp32_mkdir("sub",0700) || cp32_rename_noreplace("b","sub/c"))return 115;
  if(cp32_stat("b",&b)!=-1 || cp32_errno!=CP32_ENOENT || cp32_stat("sub/c",&b) || b.inode!=a.inode || b.links!=1)return 116;
  if(cp32_lseek(fd,0,0) || cp32_write(fd,"yo",2)!=2 || cp32_lseek(fd,0,0) || cp32_read(fd,data,2)!=2 || data[0]!='y' || data[1]!='o')return 117;
  if(cp32_link("sub/c","alias") || cp32_rename_noreplace("sub/c","alias") || cp32_stat("sub/c",&b) || b.links!=2 || cp32_rename_noreplace("alias","alias"))return 118;
  if(cp32_unlink("sub/c") || cp32_unlink("alias") || cp32_close(fd) || cp32_rmdir("sub") || cp32_chdir("/") || cp32_rmdir("ntest"))return 119;
  return 0;
}
static int rmdir_test(void)
{
  struct cp32_app_stat st;int fd;
  if(cp32_mkdir("/rtest",0700) || cp32_chdir("/rtest") || cp32_mkdir("sub",0700))return 100;
  if(cp32_rmdir("/rtest")!=-1 || cp32_errno!=CP32_EBUSY)return 101;
  if(cp32_rmdir(".")!=-1 || cp32_errno!=CP32_EINVAL)return 102;
  if(cp32_chdir("/") || cp32_rmdir("rtest")!=-1 || cp32_errno!=CP32_ENOTEMPTY)return 103;
  fd=cp32_opendir("rtest/sub");
  if(fd<0 || cp32_rmdir("rtest/sub")!=-1 || cp32_errno!=CP32_EBUSY || cp32_close(fd))return 104;
  if(cp32_rmdir("rtest/sub") || cp32_stat("rtest",&st) || st.links!=2)return 105;
  if(cp32_rmdir("rtest") || cp32_stat("rtest",&st)!=-1 || cp32_errno!=CP32_ENOENT)return 106;
  if(cp32_rmdir("/")!=-1 || cp32_errno!=CP32_EBUSY)return 107;
  if(cp32_rmdir("readme")!=-1 || cp32_errno!=CP32_ENOTDIR)return 108;
  if(cp32_mkdir("rtest",0700) || cp32_rmdir("rtest"))return 109;
  return 0;
}
static int chmod_test(void)
{
  struct cp32_app_stat before,after;char data[2];int fd;
  if(cp32_chdir("/boot"))return 90;
  fd=cp32_open_flags("../mtest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0)return 91;
  if(cp32_write(fd,"ok",2)!=2 || cp32_link("../mtest","malias") || cp32_fstat(fd,&before))return 92;
  cp32_umask(0777);
  if(cp32_chmod("malias",0444) || cp32_stat("../mtest",&after) || after.mode!=0100444 || after.inode!=before.inode || after.links!=2 || after.size!=2)return 93;
  if(cp32_open_flags("../mtest",CP32_O_WRONLY,0)!=-1 || cp32_errno!=CP32_EACCES)return 94;
  if(cp32_chmod("../mtest",0) || cp32_open("malias")!=-1 || cp32_errno!=CP32_EACCES)return 95;
  if(cp32_lseek(fd,0,0) || cp32_write(fd,"yo",2)!=2 || cp32_lseek(fd,0,0) || cp32_read(fd,data,2)!=2 || data[0]!='y' || data[1]!='o')return 96;
  if(cp32_chmod("malias",0640) || cp32_fstat(fd,&after) || after.mode!=0100640)return 97;
  if(cp32_chmod("malias",01000)!=-1 || cp32_errno!=CP32_EINVAL)return 98;
  if(cp32_unlink("../mtest") || cp32_unlink("malias") || cp32_close(fd) || cp32_chdir("/"))return 99;
  return 0;
}
static int link_test(void)
{
  struct cp32_app_stat a,b;char data[2];int fd,other;
  if(cp32_chdir("/boot"))return 80;
  fd=cp32_open_flags("../ltest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0)return 81;
  if(cp32_write(fd,"hi",2)!=2 || cp32_link("../ltest","lalias"))return 82;
  if(cp32_stat("../ltest",&a) || cp32_stat("lalias",&b) || a.inode!=b.inode || a.links!=2 || b.links!=2)return 83;
  if(cp32_link("../ltest","lalias")!=-1 || cp32_errno!=CP32_EEXIST)return 84;
  other=cp32_open_flags("lalias",CP32_O_RDWR,0);
  if(other<0 || cp32_write(other,"yo",2)!=2 || cp32_lseek(fd,0,0) || cp32_read(fd,data,2)!=2 || data[0]!='y' || data[1]!='o')return 85;
  if(cp32_unlink("../ltest") || cp32_fstat(other,&b) || b.links!=1)return 86;
  if(cp32_link("../ltest","missing")!=-1 || cp32_errno!=CP32_ENOENT)return 87;
  if(cp32_unlink("lalias") || cp32_fstat(fd,&a) || a.links || cp32_lseek(other,0,0) || cp32_read(other,data,2)!=2 || data[0]!='y' || data[1]!='o')return 88;
  if(cp32_close(fd) || cp32_close(other) || cp32_chdir("/"))return 89;
  return 0;
}
static int mkdir_test(void)
{
  struct cp32_app_stat parent,child,dot;int fd;
  cp32_umask(0077);
  if(cp32_mkdir("/dtest",0777))return 70;
  if(cp32_stat("/dtest",&parent) || (parent.mode&0777)!=0700 || parent.links!=2)return 71;
  if(cp32_mkdir("/dtest",0777)!=-1 || cp32_errno!=CP32_EEXIST)return 72;
  if(cp32_chdir("/dtest") || cp32_mkdir("sub",0755))return 73;
  if(cp32_stat("sub",&child) || (child.mode&0777)!=0700 || cp32_stat("sub/.",&dot) || dot.inode!=child.inode)return 74;
  if(cp32_stat("sub/..",&dot) || dot.inode!=parent.inode || dot.links!=3)return 75;
  fd=cp32_creat("sub/a",0666);
  if(fd<0 || cp32_write(fd,"ok",2)!=2 || cp32_close(fd))return 76;
  if(cp32_mkdir("sub/a/x",0777)!=-1 || cp32_errno!=CP32_ENOTDIR)return 77;
  if(cp32_unlink("sub/a") || cp32_chdir("/"))return 78;
  return 0;
}
static int unlink_test(void)
{
  struct cp32_app_stat old,now;char b[4];int fd,alias,reader,fresh;
  fd=cp32_open_flags("/utest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0)return 60;
  if(cp32_write(fd,"old",3)!=3 || cp32_fstat(fd,&old) || (alias=cp32_dup(fd))<0 || (reader=cp32_open("/utest"))<0)return 61;
  if(cp32_unlink("/utest") || cp32_stat("/utest",&now)!=-1 || cp32_errno!=CP32_ENOENT)return 62;
  if(cp32_fstat(fd,&now) || now.links || now.inode!=old.inode)return 63;
  fresh=cp32_open_flags("/utest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fresh<0 || cp32_write(fresh,"new",3)!=3 || cp32_fstat(fresh,&now) || now.inode==old.inode)return 64;
  if(cp32_write(alias,"!",1)!=1 || cp32_read(reader,b,4)!=4 || b[0]!='o' || b[1]!='l' || b[2]!='d' || b[3]!='!')return 65;
  if(cp32_close(fd) || cp32_close(alias) || cp32_fstat(reader,&now) || now.size!=4 || cp32_close(reader))return 66;
  if(cp32_lseek(fresh,0,0) || cp32_read(fresh,b,3)!=3 || b[0]!='n' || b[1]!='e' || b[2]!='w')return 67;
  if(cp32_unlink("/utest") || cp32_close(fresh))return 68;
  fd=cp32_open_flags("/utest",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0600);
  if(fd<0 || cp32_fstat(fd,&now) || now.inode!=old.inode || cp32_unlink("/utest") || cp32_close(fd))return 69;
  return 0;
}
static int state_test(int child)
{
  char cwd[256],data[4];int fd,copy;
  struct cp32_app_stat st;
  if(child) {
    if(!cp32_getcwd(cwd,sizeof(cwd)) || cwd[0]!='/' || cwd[1]!='b' || cwd[2]!='o' || cwd[3]!='o' || cwd[4]!='t' || cwd[5])return 51;
    if(cp32_umask(0022)!=0077)return 52;
    if(cp32_read(4,data,1)!=-1 || cp32_errno!=CP32_EBADF)return 53;
    if(cp32_lseek(3,0,1)!=2 || cp32_lseek(3,0,0) || cp32_write(3,"!",1)!=1)return 54;
    if(cp32_fstat(3,&st) || st.size!=3 || (st.mode&0777)!=0600)return 55;
    if(cp32_lseek(3,0,0) || cp32_read(3,data,3)!=3 || data[0]!='o' || data[1]!='k' || data[2]!='!')return 56;
    return 0; /* Exit owns final descriptor cleanup. */
  }
  if(cp32_chdir("/boot") || !cp32_getcwd(cwd,sizeof(cwd)))return 40;
  if(cp32_umask(0077)!=0022)return 41;
  fd=cp32_open_flags("/statefile",CP32_O_RDWR|CP32_O_CREAT|CP32_O_EXCL,0666);
  if(fd!=3 || cp32_write(fd,"ok",2)!=2 || (copy=cp32_dup(fd))!=4)return 42;
  if(cp32_fcntl(copy,CP32_F_SETFD,CP32_FD_CLOEXEC) || cp32_fcntl(fd,CP32_F_GETFD,0))return 43;
  if(cp32_fcntl(copy,CP32_F_SETFL,CP32_O_APPEND) || cp32_fcntl(fd,CP32_F_GETFL,0)!=(CP32_O_RDWR|CP32_O_APPEND))return 44;
  {
    const char *args[]={"hello","--state-child",0};
    if(cp32_execv("/missing",args)!=-1 || cp32_errno!=CP32_ENOENT || cp32_fcntl(copy,CP32_F_GETFD,0)!=1)return 45;
    cp32_execv("hello",args); /* relative to the child's /boot, not shell cwd */
  }
  return 46;
}
#endif
int app_main(unsigned argc,char **argv,char **envp,const struct cp32_app_services *s)
{
  unsigned i,n;

  if(!s || (s->version<1 || s->version>22) || !s->write || !s->exit || started) return 1;
  started=1;
#if !CP32_ROOT_RAM
  if(option(argc,argv,"--space")) {
    int r;if(cp32_io_init(s))return 79;r=space_test();
    if(!r)cp32_write(1,"Space OK\n",9);
    return r;
  }
  if(option(argc,argv,"--at")) {
    int r;if(cp32_io_init(s))return 79;r=position_test();
    if(!r)cp32_write(1,"PosIO OK\n",9);
    return r;
  }
  if(option(argc,argv,"--fmode")) {
    int r;if(cp32_io_init(s))return 79;r=fchmod_test();
    if(!r)cp32_write(1,"Fchmod OK\n",10);
    return r;
  }
  if(option(argc,argv,"--fcntl")) {
    int r;if(cp32_io_init(s))return 79;r=dupfd_test();
    if(!r)cp32_write(1,"Dupfd OK\n",9);
    return r;
  }
  if(option(argc,argv,"--dup")) {
    int r;if(cp32_io_init(s))return 79;r=dup2_test();
    if(!r)cp32_write(1,"Dup2 OK\n",8);
    return r;
  }
  if(option(argc,argv,"--size")) {
    int r;if(cp32_io_init(s))return 79;r=resize_test();
    if(!r)cp32_write(1,"Resize OK\n",10);
    return r;
  }
  if(option(argc,argv,"--move")) {
    int r;if(cp32_io_init(s))return 79;r=rename_test();
    if(!r)cp32_write(1,"Rename OK\n",10);
    return r;
  }
  if(option(argc,argv,"--rmdir")) {
    int r;if(cp32_io_init(s))return 79;r=rmdir_test();
    if(!r)cp32_write(1,"Rmdir OK\n",9);
    return r;
  }
  if(option(argc,argv,"--mode")) {
    int r;if(cp32_io_init(s))return 79;r=chmod_test();
    if(!r)cp32_write(1,"Chmod OK\n",9);
    return r;
  }
  if(option(argc,argv,"--link")) {
    int r;if(cp32_io_init(s))return 79;r=link_test();
    if(!r)cp32_write(1,"Link OK\n",8);
    return r;
  }
  if(option(argc,argv,"--dir")) {
    int r;if(cp32_io_init(s))return 79;r=mkdir_test();
    if(!r)cp32_write(1,"Mkdir OK\n",9);
    return r;
  }
  if(option(argc,argv,"--unlink")) {
    int r;if(cp32_io_init(s))return 59;r=unlink_test();
    if(!r)cp32_write(1,"Unlink OK\n",10);
    return r;
  }
  if(option(argc,argv,"--state") || option(argc,argv,"--state-child")) {
    int r;if(cp32_io_init(s))return 39;
    r=state_test(option(argc,argv,"--state-child"));
    if(!r)cp32_write(1,"FS state OK\n",12);
    return r;
  }
#endif
  if(option(argc,argv,"--file") || option(argc,argv,"--check")) {
    int r;if(cp32_io_init(s))return 19;
    r=option(argc,argv,"--file") ? io_test():io_check();
    if(!r)cp32_write(1,"File IO OK\n",11);
    else {
      /* Preserve errno before writing the diagnostic itself. */
      unsigned error=cp32_errno;
      char message[]="File IO stage=00 errno=00\n";
      message[14]='0'+r/10;message[15]='0'+r%10;
      message[24]='0'+error%10;message[23]='0'+(error/10)%10;
      cp32_write(1,message,sizeof(message)-1);
    }
    return r;
  }

  if(option(argc,argv,"--exec") || option(argc,argv,"--exec-fail")) {
    int pid;char a[8];
    const char *next[]={"hello","--exec-child",0};
    if(cp32_io_init(s) || !s->getpid || (pid=s->getpid())<=0)return 1;
    if(cp32_execv("/missing",next)!=-1 || cp32_errno!=CP32_ENOENT || s->getpid()!=pid)return 1;
    if(cp32_execv("/readme",next)!=-1 || cp32_errno!=CP32_EACCES)return 1;
    if(option(argc,argv,"--exec-fail"))return cp32_write(1,"Exec failure recovery OK\n",24)==24 ? 0:1;
    if(cp32_open("/readme")!=3 || cp32_read(3,a,5)!=5)return 1;
    /* Pass the original PID as an argv string; next image verifies identity. */
    char id[12];unsigned n=0,value=pid,j;
    do {id[n++]='0'+value%10;value/=10;}while(value);
    for(j=0;j<n/2;j++){char c=id[j];id[j]=id[n-1-j];id[n-1-j]=c;}id[n]=0;
    const char *args[]={"hello","--exec-child",id,0};
    cp32_execv("/boot/hello",args);return 1;
  }
  if(argc==3 && option(2,argv,"--exec-child")) {
    unsigned pid=0,j;char a[8];
    for(j=0;argv[2][j];j++) {if(argv[2][j]<'0' || argv[2][j]>'9')return 1;pid=pid*10+argv[2][j]-'0';}
    if(cp32_io_init(s) || !s->getpid || (unsigned)s->getpid()!=pid ||
       cp32_lseek(3,0,1)!=5 || cp32_read(3,a,5)!=5 || cp32_close(3))return 1;
    return cp32_write(1,"Exec PID and file OK\n",20)==20 ? 0:1;
  }
  if(option(argc,argv,"--files")) {
    int fd[4],k;char a[8],b[8];
    if(cp32_io_init(s))return 1;
    for(k=0;k<4;k++)if((fd[k]=cp32_open("/readme"))!=k+3)return 1;
    if(cp32_open("/readme")!=-1 || cp32_errno!=CP32_EMFILE)return 1;
    if(cp32_read(fd[0],a,5)!=5 || cp32_read(fd[1],b,5)!=5)return 1;
    for(k=0;k<5;k++)if(a[k]!=b[k])return 1;
    if(cp32_lseek(fd[0],0,1)!=5 || cp32_lseek(fd[1],0,1)!=5 ||
       cp32_lseek(fd[0],-1,2)!=60 || cp32_read(fd[0],a,8)!=1 || a[0]!='\n' ||
       cp32_read(fd[0],a,8)!=0)return 1;
    if(cp32_close(fd[0]) || cp32_close(fd[0])!=-1 || cp32_errno!=CP32_EBADF)return 1;
    if(cp32_open("/missing")!=-1 || cp32_errno!=CP32_ENOENT)return 1;
    if(cp32_open("/boot")!=-1 || cp32_errno!=CP32_EISDIR)return 1;
    /* Deliberately leave three handles to exercise normal-exit cleanup. */
    return cp32_write(1,"File API OK\n",12)==12 ? 0 : 1;
  }
  if(option(argc,argv,"--errno")) {
    char byte;
    if(cp32_io_init(s)) return 1;
    if(cp32_read(1,&byte,1)!=-1 || cp32_errno!=CP32_EBADF ||
       cp32_write(1,0,1)!=-1 || cp32_errno!=CP32_EFAULT ||
       cp32_readline(&byte,1)!=-1 || cp32_errno!=CP32_EINVAL ||
       cp32_write(1,0,0)!=0 || cp32_errno!=CP32_EINVAL) return 1;
    return cp32_write(1,"I/O errno OK\n",13)==13 ? 0 : 1;
  }
  if(option(argc,argv,"--line")) {
    char line[16];int count;
    if(cp32_io_init(s) || cp32_write(1,"Line: ",6)!=6) return 1;
    do {
      count=cp32_readline(line,sizeof(line));
      if(count<0) return 1;
      if(cp32_write(1,line,(unsigned)count)!=count) return 1;
    } while(count && line[count-1]!='\n');
    return 0;
  }
  if(option(argc,argv,"--io")) {
    char line[64];int count;
    if(cp32_io_init(s) || cp32_write(1,"Standard input: ",16)!=16) return 1;
    count=cp32_read(0,line,sizeof(line));
    if(count<0 || cp32_write(2,"Read: ",6)!=6 ||
       cp32_write(1,line,(unsigned)count)!=count) return 1;
    if(cp32_read(1,line,1)!=-1 || cp32_write(0,line,1)!=-1) return 1;
    return 0;
  }
  if(option(argc,argv,"--read")) {
    char line[64];int count;
    if(s->version<5 || !s->read) return 1;
    if(s->write("Type a line: ",13)!=13) return 1;
    count=s->read(line,sizeof(line));
    if(count<0 || count>(int)sizeof(line)) return 1;
    if(s->write("Read: ",6)!=6 || s->write(line,(unsigned)count)!=count) return 1;
    return 0;
  }
  if(option(argc,argv,"--trim")) {
    void *base,*a,*b,*top;
    if(cp32_heap_init(s)) return 1;
    base=s->sbrk(0);a=cp32_malloc(32);top=s->sbrk(0);b=cp32_malloc(64);
    if(!a || !b || cp32_heap_trim()!=0) return 1;
    cp32_free(b);
    if(cp32_heap_trim()<=0 || s->sbrk(0)!=top) return 1;
    cp32_free(a);
    if(cp32_heap_trim()<=0 || s->sbrk(0)!=base || cp32_heap_trim()!=0) return 1;
    a=cp32_malloc(32);if(!a) return 1;cp32_free(a);
    if(cp32_heap_trim()<=0 || s->sbrk(0)!=base) return 1;
    return s->write("Heap trim OK\n",13)==13 ? 0 : 1;
  }
  if(option(argc,argv,"--resize")) {
    unsigned char *a,*b,*c; void *top;
    if(cp32_heap_init(s)) return 1;
    a=cp32_malloc(32); b=cp32_malloc(96); c=cp32_malloc(32);
    if(!a || !b || !c) return 1;
    for(i=0;i<32;i++) {a[i]=(unsigned char)i;c[i]=77;}
    top=s->sbrk(0); cp32_free(b);
    if(cp32_realloc(a,112)!=a || s->sbrk(0)!=top) return 1;
    for(i=0;i<32;i++) if(a[i]!=(unsigned char)i || c[i]!=77) return 1;
    if(cp32_realloc(a,16)!=a) return 1;
    b=cp32_malloc(64);
    if(!b || s->sbrk(0)!=top) return 1;
    cp32_free(a);cp32_free(b);cp32_free(c);
    return s->write("Resize in place OK\n",sizeof("Resize in place OK\n")-1)==
      (int)(sizeof("Resize in place OK\n")-1) ? 0 : 1;
  }
  if(option(argc,argv,"--alloc")) {
    unsigned char *a,*b;
    if(cp32_heap_init(s)) return 1;
    a=cp32_calloc(4,8);
    if(!a) return 1;
    for(i=0;i<32;i++) { if(a[i]) return 1; a[i]=(unsigned char)(i+1); }
    b=cp32_realloc(a,96);
    if(!b) return 1;
    for(i=0;i<32;i++) if(b[i]!=(unsigned char)(i+1)) return 1;
    if(cp32_realloc(b,0xffffffffU) || cp32_calloc(0x80000000U,2)) return 1;
    for(i=0;i<32;i++) if(b[i]!=(unsigned char)(i+1)) return 1;
    if(cp32_realloc(b,16)!=b || cp32_realloc(b,0)) return 1;
    a=cp32_calloc(4,8);
    if(!a) return 1;
    for(i=0;i<32;i++) if(a[i]) return 1;
    cp32_free(a);
    return s->write("Calloc/realloc OK\n",sizeof("Calloc/realloc OK\n")-1)==(int)(sizeof("Calloc/realloc OK\n")-1) ? 0 : 1;
  }
  if(option(argc,argv,"--malloc")) {
    unsigned char *a,*b,*c;
    if(cp32_heap_init(s)) return 1;
    a=cp32_malloc(32); b=cp32_malloc(48);
    if(!a || !b || a==b) return 1;
    for(i=0;i<32;i++) a[i]=(unsigned char)i;
    for(i=0;i<48;i++) b[i]=(unsigned char)(i+32);
    cp32_free(a); c=cp32_malloc(16);
    if(c!=a) return 1;
    for(i=0;i<48;i++) if(b[i]!=(unsigned char)(i+32)) return 1;
    cp32_free(c); cp32_free(b);
    c=cp32_malloc(80);
    if(c!=a || cp32_malloc(0xffffffffU)) return 1;
    cp32_free(c);
    return s->write("Malloc/free OK\n",15)==15 ? 0 : 1;
  }
  if(option(argc,argv,"--heap")) {
    unsigned char *base;
    if(s->version<4 || !s->sbrk) return 1;
    base=s->sbrk(0);
    if(base==(void *)-1 || s->sbrk(32)!=base) return 1;
    for(i=0;i<32;i++) { if(base[i]) return 1; base[i]=(unsigned char)(i+1); }
    if(s->sbrk(0)!=base+32 || s->sbrk(0x7fffffff)!=(void *)-1 ||
       s->sbrk(0)!=base+32 || s->sbrk(-32)!=base+32 ||
       s->sbrk(-1)!=(void *)-1 || s->sbrk(32)!=base) return 1;
    for(i=0;i<32;i++) if(base[i]) return 1;
    if(s->sbrk(-32)!=base+32) return 1;
    return s->write("Heap grow/shrink OK\n",20)==20 ? 0 : 1;
  }
  if(argc==2 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='p' &&
     ((argv[1][3]=='i' && argv[1][4]=='d' && !argv[1][5]) ||
      (argv[1][3]=='p' && argv[1][4]=='i' && argv[1][5]=='d' && !argv[1][6]))) {
    char text[12]; unsigned value,pos=sizeof(text);
    int pid,parent=argv[1][3]=='p';
    if(parent) {
      if(s->version<3 || !s->getppid || (pid=s->getppid())<=0) return 1;
    } else if(s->version<2 || !s->getpid || (pid=s->getpid())<=0) return 1;
    value=(unsigned)pid; text[--pos]='\n';
    do { text[--pos]='0'+value%10; value/=10; } while(value);
    if(s->write(parent ? "PPID=" : "PID=",parent ? 5 : 4)!=(parent ? 5 : 4) ||
       s->write(text+pos,sizeof(text)-pos)!=(int)(sizeof(text)-pos)) return 1;
    return 0;
  }
  /* Exercise explicit exit, independently of crt0's main-return path.
   * Bound decimal parsing without depending on a hosted libc. */
  if(argc>1 && argv[1][0]=='-' && argv[1][1]=='-' &&
     argv[1][2]=='e' && argv[1][3]=='x' && argv[1][4]=='i' &&
     argv[1][5]=='t' && argv[1][6]==0) {
    unsigned value=0;
    if(argc!=3 || !argv[2][0]) goto usage;
    for(n=0;argv[2][n];n++) {
      unsigned digit=(unsigned char)argv[2][n]-'0';
      if(digit>9 || value>(65535U-digit)/10U) goto usage;
      value=value*10+digit;
    }
    s->exit((int)value);
    return 1; /* A broken service must not fall through to the greeting. */
  }
  if(option(argc,argv,"--env")) {
    for(i=0;envp && envp[i];i++) {
      for(n=0;envp[i][n];n++) {}
      if(s->write(envp[i],n)!=(int)n || s->write("\n",1)!=1) return 1;
    }
    return 0;
  }
  if(s->write(greeting,sizeof(greeting)-1)!=(int)(sizeof(greeting)-1)) return 1;
  for(i=1;i<argc;i++) {
    for(n=0;argv[i][n];n++) {}
    if(s->write("arg: ",5)!=5 || s->write(argv[i],n)!=(int)n ||
       s->write("\n",1)!=1) return 1;
  }
  return 0;
usage:
  s->write("Usage: hello --exit 0..65535\n",
           sizeof("Usage: hello --exit 0..65535\n")-1);
  return 2;
}
