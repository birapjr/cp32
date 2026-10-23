"""Production file bridge + MINIX disk + application I/O/utility integration."""
from pathlib import Path
import subprocess, sys, tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
sys.path.insert(0,str(root/'tools'))
from make_minix_demo import build_image
shell=root/'src/kernel/cp32-shell.c'
code='\n'.join(extract_function(shell,n) for n in ('cp32_shell_app_files_reset','cp32_shell_app_file'))
request=extract_function(root/'src/kernel/application.c','application_file_request')
pre=r'''
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fs.h"
#include "io.h"
static unsigned char disk[65536],arena[512];
#define CP32_APP_DATA ((uintptr_t)arena)
#define CP32_APP_TOP ((uintptr_t)(arena+sizeof(arena)))
typedef struct {int m1_i1,m1_i2,m1_i3;char *m1_p1;} message;
#include "fproc.h"
static struct cp32_fproc cp32_shell_state={.pid=1,.root_inode=1,.cwd="/"};
static struct cp32_fproc cp32_app_state={.pid=123,.root_inode=1,.mask=0022,.cwd="/"};

#define cp32_shell_cwd cp32_app_state.cwd
static unsigned cp32_root_capacity(void) {return sizeof(disk);}
static int cp32_shell_disk_write(unsigned off,const char *p,int n) {
 if(off>sizeof(disk) || (unsigned)n>sizeof(disk)-off)return -1;
 memcpy(disk+off,p,n);return n;
}
static int cp32_shell_disk_read(unsigned off,char *out,int count) {
 if(off>sizeof(disk) || (unsigned)count>sizeof(disk)-off)return -1;
 memcpy(out,disk+off,count);return count;
}
static char output[4096];static unsigned used;static int fail_output;
static int put(const char *p,unsigned n) {if(fail_output)return -1;assert(used+n<sizeof(output));memcpy(output+used,p,n);used+=n;return n;}
static void quit(int status) {(void)status;assert(0);}
static int input(char *p,unsigned n) {(void)p;(void)n;return 0;}
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
'''
test=r'''
#ifdef TEST_HELLO
static jmp_buf replaced;
static int state_bridge(unsigned op,int fd,void *buffer,unsigned n)
{
 if(op==CP32_APP_EXEC) {
   struct cp32_app_exec_request *q=buffer;
   assert(n==sizeof(*q));
   if(!strcmp(q->path,"/missing"))return -2;
   assert(!strcmp(q->path,"hello") && q->argc==2 && !strcmp(q->args+6,"--state-child"));
   cp32_fproc_exec(&cp32_app_state);longjmp(replaced,1);
 }
 return cp32_shell_app_file(op,fd,buffer,n);
}
#endif
int main(int argc,char **argv) {
 assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);
 assert(fread(disk,1,sizeof(disk),f)==sizeof(disk));fclose(f);
 int mode=argv[2][0]-'0';
 struct cp32_app_services api={7,put,quit,0,0,0,input,cp32_shell_app_file};
#ifdef TEST_HELLO
 if(mode==20) {
   api.version=22;char *args[]={"hello","--space",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"space stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Space OK"));cp32_shell_app_files_reset();return 0;
 }
 if(mode==19) {
   api.version=21;char *args[]={"hello","--at",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"position stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"PosIO OK"));
   struct cp32_app_stat st;assert(cp32_stat("/ptest",&st)==-1 && cp32_errno==CP32_ENOENT);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==18) {
   api.version=20;char *args[]={"hello","--fmode",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"fchmod stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Fchmod OK"));
   struct cp32_app_stat st;assert(cp32_stat("/htest",&st)==-1 && cp32_errno==CP32_ENOENT);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==17) {
   api.version=19;char *args[]={"hello","--fcntl",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"dupfd stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Dupfd OK"));
   struct cp32_app_stat st;assert(cp32_stat("/ftest",&st)==-1 && cp32_errno==CP32_ENOENT);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==16) {
   int held[4];for(int i=0;i<4;i++){held[i]=cp32_fd_open(cp32_shell_disk_read,sizeof(disk),"/readme");assert(held[i]>=0);}
   api.version=18;char *args[]={"hello","--dup",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"dup2 stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Dup2 OK"));
   for(int i=0;i<4;i++)assert(!cp32_fd_close(held[i]));
   struct cp32_app_stat st;assert(cp32_stat("/qtest",&st)==-1 && cp32_errno==CP32_ENOENT);
   assert(cp32_stat("/qold",&st)==-1 && cp32_errno==CP32_ENOENT);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==15) {
   api.version=17;char *args[]={"hello","--size",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"resize stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Resize OK"));
   struct cp32_app_stat st;assert(cp32_stat("/ztest",&st)==-1 && cp32_errno==CP32_ENOENT);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==14) {
   api.version=16;char *args[]={"hello","--move",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"rename stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Rename OK"));
   struct cp32_app_stat st;assert(cp32_stat("/ntest",&st)==-1 && cp32_errno==CP32_ENOENT);
   assert(cp32_rename_noreplace("/absent","/x")==-1 && cp32_errno==CP32_ENOENT);
   assert(cp32_rename_noreplace("/boot","/x")==-1 && cp32_errno==CP32_EISDIR);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==13) {
   api.version=15;char *args[]={"hello","--rmdir",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"rmdir stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Rmdir OK"));
   assert(!cp32_mkdir("/guard",0700));strcpy(cp32_shell_state.cwd,"/guard");
   assert(cp32_rmdir("/guard")==-1 && cp32_errno==CP32_EBUSY);
   strcpy(cp32_shell_state.cwd,"/");assert(!cp32_rmdir("/guard"));
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==12) {
   api.version=14;char *args[]={"hello","--mode",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"chmod stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Chmod OK"));
   struct cp32_app_stat st;assert(cp32_stat("/mtest",&st)==-1 && cp32_errno==CP32_ENOENT);
   assert(cp32_stat("/boot/malias",&st)==-1 && cp32_errno==CP32_ENOENT);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==11) {
   api.version=13;char *args[]={"hello","--link",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"link stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Link OK"));
   struct cp32_app_stat st;assert(cp32_stat("/ltest",&st)==-1 && cp32_errno==CP32_ENOENT);
   assert(cp32_stat("/boot/lalias",&st)==-1 && cp32_errno==CP32_ENOENT);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==10) {
   api.version=12;char *args[]={"hello","--dir",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"mkdir stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Mkdir OK"));
   assert(!cp32_minix_rmdir(cp32_shell_disk_read,cp32_shell_disk_write,sizeof(disk),"/dtest/sub","/"));
   assert(!cp32_minix_rmdir(cp32_shell_disk_read,cp32_shell_disk_write,sizeof(disk),"/dtest","/"));
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==9) {
   api.version=11;char *args[]={"hello","--unlink",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"unlink stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Unlink OK"));cp32_shell_app_files_reset();return 0;
 }
 if(mode==7) {
   api.version=10;api.file=state_bridge;
   char *args[]={"hello","--state",0};
   if(!setjmp(replaced)){app_main(2,args,0,&api);assert(0);}
   assert(cp32_app_state.mask==0077 && !strcmp(cp32_app_state.cwd,"/boot"));
   assert(cp32_app_state.fds[0] && !cp32_app_state.fds[1]);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==8) {
   api.version=10;strcpy(cp32_app_state.cwd,"/boot");cp32_app_state.mask=0077;
   struct cp32_app_open_request q={"/statefile",CP32_O_RDWR|CP32_O_CREAT|CP32_O_TRUNC,0600};
   assert(cp32_shell_app_file(CP32_APP_OPEN_MODE,0,&q,sizeof(q))==3);
   assert(cp32_shell_app_file(CP32_APP_WRITE,3,"ok",2)==2);
   assert(!cp32_shell_app_file(CP32_APP_SETFL,3,0,CP32_O_APPEND));
   char *args[]={"hello","--state-child",0};
   assert(!app_main(2,args,0,&api) && strstr(output,"FS state OK"));
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==5 || mode==6) {
   api.version=9;
   if(mode==6)assert(!cp32_minix_unlink(cp32_shell_disk_read,cp32_shell_disk_write,sizeof(disk),"/wtest"));
   char *args[]={"hello",mode==5 ? "--file":"--check",0};
   assert(app_main(2,args,0,&api)==(mode==5 ? 20:31));
   assert(strstr(output,mode==5 ? "File IO stage=20 errno=17":"File IO stage=31 errno=02"));
   cp32_shell_app_files_reset();cp32_app_state.pid=123;return 0;
 }
 if(mode==3 || mode==4) {
   api.version=9;char *args[]={"hello",mode==3 ? "--file":"--check",0};
   int result=app_main(2,args,0,&api);if(result)fprintf(stderr,"file app stage %d\n",result);
   assert(!result);assert(strstr(output,"File IO OK"));
   cp32_shell_app_files_reset();cp32_app_state.pid=123;
   if(mode==3){f=fopen(argv[1],"wb");assert(f && fwrite(disk,1,sizeof(disk),f)==sizeof(disk));fclose(f);}
   return 0;
 }
#endif
 if(mode) {
#ifdef TEST_LS
   char *args[]={"ls","-al","/boot/large",0};unsigned count=3;
   if(mode==2){args[1]="/";count=2;}
   if(mode==3){args[1]="missing";args[2]="/readme";}
   if(mode==4){args[1]="-z";count=2;}
   if(mode==5){args[1]="-ld";args[2]="/boot";}
   if(mode==6){args[1]="-l";args[2]="/readme";fail_output=1;}
   if(mode==7){count=1;strcpy(cp32_shell_cwd,"/boot/large");}
   int r=app_main(count,args,0,&api);
   assert(r==(mode==3 || mode==6 ? 1:mode==4 ? 2:0));
   if(mode==1)assert(!strcmp(output,"drwxr-xr-x 2 0 0 7184 .\ndrwxr-xr-x 3 0 0 96 ..\n-r--r--r-- 3 0 0 61 readme\n"));
   if(mode==2)assert(!strcmp(output,"boot\nreadme\n"));
   if(mode==3)assert(strstr(output,"ls: cannot list path\n") && strstr(output,"/readme\n"));
   if(mode==4)assert(strstr(output,"Usage:"));
   if(mode==5)assert(!strcmp(output,"drwxr-xr-x 3 0 0 96 /boot\n"));
   if(mode==7)assert(!strcmp(output,"readme\n"));
   for(int k=0;k<4;k++)assert(!cp32_app_state.fds[k]);
   return 0;
#elif defined(TEST_HELLO)
   char *args[]={"hello","--files",0};
   assert(!app_main(2,args,0,&api));
   assert(!strcmp(output,"File API OK\n"));
   assert(!cp32_app_state.fds[0] && cp32_app_state.fds[1] && cp32_app_state.fds[2] && cp32_app_state.fds[3]);
   cp32_shell_app_files_reset();cp32_app_state.pid=123;
   for(int i=0;i<4;i++)assert(cp32_shell_app_file(CP32_APP_OPEN,0,"/readme",8)==i+3);
   cp32_shell_app_files_reset();cp32_app_state.pid=123;return 0;
#else
   char *args[]={"app","readme","boot/readme",0};
   if(mode==2)args[1]="missing";
   int r=app_main(3,args,0,&api);
   assert(r==(mode==2 ? 1:0));
#ifdef TEST_CAT
   assert(strstr(output,"CP32 MINIX V2 RAM filesystem."));
   if(mode==1)assert(used==122);
#else
   assert(strstr(output,"2 8 61 boot/readme\n"));
   if(mode==1)assert(strstr(output,"4 16 122 total\n"));
#endif
   for(unsigned i=0;i<CP32_APP_FILE_MAX;i++)assert(!cp32_app_state.fds[i]);
   return 0;
#endif
 }
 assert(!cp32_io_init(&api));
 /* The shell's live descriptors cannot be accessed or closed by the app. */
 int shellfd=cp32_fd_open(cp32_shell_disk_read,sizeof(disk),"/readme");assert(shellfd>=0);
 assert(cp32_close(2)==-1 && cp32_errno==CP32_EBADF);
 assert(cp32_open("missing")==-1 && cp32_errno==CP32_ENOENT);
 assert(cp32_open("boot")==-1 && cp32_errno==CP32_EISDIR);
 assert(cp32_open("readme/no")==-1 && cp32_errno==CP32_ENOTDIR);
 int a=cp32_open("readme"),b=cp32_open("/boot/readme");assert(a==3 && b==4);
 char x[64],y[64];
 assert(cp32_read(a,x,5)==5 && cp32_read(b,y,5)==5 && !memcmp(x,y,5));
 assert(cp32_lseek(a,0,1)==5 && cp32_lseek(b,0,1)==5);
 assert(cp32_lseek(a,-1,0)==-1 && cp32_errno==CP32_EINVAL);
 assert(cp32_lseek(a,-1,2)==60 && cp32_read(a,x,64)==1 && *x=='\n');
 assert(!cp32_read(a,x,64));
 assert(cp32_open("readme")==5 && cp32_open("readme")==6);
 assert(cp32_open("readme")==-1 && cp32_errno==CP32_EMFILE);
 assert(!cp32_close(a));assert(cp32_read(a,x,0)==-1 && cp32_errno==CP32_EBADF);
 assert(cp32_close(a)==-1 && cp32_errno==CP32_EBADF);
 cp32_shell_app_files_reset();cp32_app_state.pid=123;
 assert(cp32_fd_read(shellfd,x,5)==5 && !memcmp(x,"CP32 ",5));
 assert(!cp32_fd_close(shellfd));
 for(int j=0;j<40;j++) {
   for(int i=0;i<4;i++)assert(cp32_open("readme")==i+3);
   cp32_shell_app_files_reset();cp32_app_state.pid=123; /* intentionally leaked handles at exit */
 }
 strcpy(cp32_shell_cwd,"/boot");
 a=cp32_open("indirect");assert(a==3);
 assert(cp32_lseek(a,7168,0)==7168 && cp32_read(a,x,64)==17 && !memcmp(x,"INDIRECT READ OK\n",17));
 assert(!cp32_close(a));
 a=cp32_open("double");assert(a==3);
 assert(cp32_lseek(a,263*1024,0)==263*1024 && cp32_read(a,x,23)==23);
 assert(!memcmp(x,"Double indirect line 01",23));
 cp32_shell_app_files_reset();cp32_app_state.pid=123;
 /* Directory and stat APIs share ownership but not offsets. */
 struct cp32_app_stat st,old;
 struct cp32_app_dirent e,oldentry;
 assert(!cp32_stat("readme",&st) && st.size==61 && st.links==3 && st.mode==0100444);
 old=st;assert(cp32_stat("missing",&st)==-1 && cp32_errno==CP32_ENOENT && !memcmp(&st,&old,sizeof(st)));
 assert(cp32_opendir("readme")==-1 && cp32_errno==CP32_ENOTDIR);
 a=cp32_opendir("large");b=cp32_opendir("large");assert(a==3 && b==4);
 assert(!cp32_fstat(a,&st) && st.size==7184 && st.mode==0040755);
 assert(cp32_readdir(a,&e)==1 && !strcmp(e.name,"."));
 assert(cp32_readdir(a,&e)==1 && !strcmp(e.name,".."));
 assert(cp32_readdir(b,&e)==1 && !strcmp(e.name,"."));
 assert(cp32_readdir(a,&e)==1 && !strcmp(e.name,"readme"));
 oldentry=e;assert(!cp32_readdir(a,&e) && !memcmp(&e,&oldentry,sizeof(e)));
 assert(cp32_lseek(a,0,0)==0 && cp32_readdir(a,&e)==1 && !strcmp(e.name,"."));
 assert(!cp32_close(a));assert(cp32_fstat(a,&st)==-1 && cp32_errno==CP32_EBADF);
 a=cp32_open("readme");oldentry=e;
 assert(cp32_readdir(a,&e)==-1 && cp32_errno==CP32_ENOTDIR && !memcmp(&e,&oldentry,sizeof(e)));
 cp32_shell_app_files_reset();cp32_app_state.pid=123;
 for(int k=0;k<4;k++)assert(cp32_opendir("large")==k+3);
 assert(cp32_opendir("large")==-1 && cp32_errno==CP32_EMFILE);
 assert(!cp32_stat("readme",&st)); /* stat needs no spare descriptor */
 cp32_shell_app_files_reset();cp32_app_state.pid=123;
 /* Production IPC span validation: reject out-of-range, overlong, unterminated. */
 message m={CP32_APP_OPEN,0,7,(char *)arena};memcpy(arena,"readme",7);
 assert(application_file_request(&m)==3);
 m.m1_p1=(char *)arena+510;assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena;m.m1_i3=257;assert(application_file_request(&m)==-36);
 m.m1_i3=6;assert(application_file_request(&m)==-22);
 m.m1_i1=CP32_APP_READ;m.m1_i2=3;m.m1_i3=65;assert(application_file_request(&m)==-22);
 m.m1_i3=64;assert(application_file_request(&m)==61 && !memcmp(arena,"CP32 ",5));
 m.m1_p1=0;m.m1_i3=0;assert(application_file_request(&m)==0);
 m.m1_i1=CP32_APP_FSTAT;m.m1_p1=(char *)arena;m.m1_i3=sizeof(st)-1;
 assert(application_file_request(&m)==-22);
 m.m1_i3=sizeof(st);m.m1_p1=(char *)arena+500;assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena+1;assert(application_file_request(&m)==0); /* unaligned user buffer */
 m.m1_i1=CP32_APP_READDIR;m.m1_i3=sizeof(e);assert(application_file_request(&m)==-20);
 m.m1_i1=CP32_APP_STAT;m.m1_i3=sizeof(struct cp32_app_stat_request);m.m1_p1=(char *)arena;
 memset(arena,0,sizeof(arena));memcpy(arena,"readme",7);assert(!application_file_request(&m));
 arena[255]='x';assert(application_file_request(&m)==-36);
 m.m1_i1=99;assert(application_file_request(&m)==-22);
 m.m1_i1=CP32_APP_OPEN_MODE;m.m1_i3=sizeof(struct cp32_app_open_request)-1;
 assert(application_file_request(&m)==-22);
 m.m1_i3=sizeof(struct cp32_app_open_request);m.m1_p1=(char *)arena+1;assert(application_file_request(&m)==-22);
 m.m1_p1=(char *)arena+sizeof(arena)-4;assert(application_file_request(&m)==-14);
 m.m1_i1=CP32_APP_CHDIR;m.m1_p1=(char *)arena;m.m1_i3=2;arena[0]='/';arena[1]='x';
 assert(application_file_request(&m)==-22);
 m.m1_p1=(char *)(CP32_APP_TOP-1);m.m1_i3=2;assert(application_file_request(&m)==-14);
 m.m1_i1=CP32_APP_FSINFO;m.m1_p1=(char *)arena;m.m1_i3=23;assert(application_file_request(&m)==-22);
 m.m1_i3=24;m.m1_p1=(char *)(CP32_APP_TOP-4);assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena+1;assert(!application_file_request(&m));
 m.m1_i1=CP32_APP_PREAD;m.m1_p1=(char *)arena;m.m1_i3=71;assert(application_file_request(&m)==-22);
 m.m1_i3=72;m.m1_p1=(char *)arena+1;assert(application_file_request(&m)==-22);
 m.m1_p1=(char *)(CP32_APP_TOP-4);assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena;memset(arena,0,sizeof(arena));m.m1_i2=3;
 ((struct cp32_app_position_request *)arena)->count=65;assert(application_file_request(&m)==-22);
 ((struct cp32_app_position_request *)arena)->count=2;((struct cp32_app_position_request *)arena)->offset=0x7fffffffU;assert(application_file_request(&m)==-22);
 m.m1_i1=CP32_APP_PWRITE;assert(application_file_request(&m)==-22);
 m.m1_i1=CP32_APP_RENAME_NOREPLACE;m.m1_p1=(char *)arena;m.m1_i3=511;assert(application_file_request(&m)==-22);
 m.m1_i3=512;m.m1_p1=(char *)arena+1;assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena;memset(arena,0,sizeof(arena));arena[255]='x';assert(application_file_request(&m)==-36);
 arena[255]=0;arena[511]='x';assert(application_file_request(&m)==-36);
 arena[511]=0;assert(application_file_request(&m)==-2);
 m.m1_i1=CP32_APP_RMDIR;m.m1_p1=(char *)arena;m.m1_i3=0;assert(application_file_request(&m)==-36);
 m.m1_i3=257;assert(application_file_request(&m)==-36);
 m.m1_i3=2;m.m1_p1=(char *)(CP32_APP_TOP-1);assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena;arena[1]='x';assert(application_file_request(&m)==-22);
 m.m1_i1=CP32_APP_CHMOD;m.m1_p1=(char *)arena;m.m1_i3=263;
 assert(application_file_request(&m)==-22);
 m.m1_i3=264;m.m1_p1=(char *)arena+256;assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena+1;assert(application_file_request(&m)==-22);
 m.m1_p1=(char *)arena;memset(arena,0,sizeof(arena));arena[255]='x';assert(application_file_request(&m)==-22);
 arena[255]=0;assert(application_file_request(&m)==-2);
 memcpy(arena,"/readme",8);((struct cp32_app_open_request *)arena)->flags=1;assert(application_file_request(&m)==-22);
 ((struct cp32_app_open_request *)arena)->flags=0;((struct cp32_app_open_request *)arena)->mode=01000;assert(application_file_request(&m)==-22);
 m.m1_i1=CP32_APP_LINK;m.m1_p1=(char *)arena;m.m1_i3=511;
 assert(application_file_request(&m)==-22);
 m.m1_i3=512;m.m1_p1=(char *)arena+1;assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena;memset(arena,0,sizeof(arena));arena[255]='x';assert(application_file_request(&m)==-36);
 arena[255]=0;arena[511]='x';assert(application_file_request(&m)==-36);
 arena[511]=0;assert(application_file_request(&m)==-2);
 memcpy(arena,"/boot",6);memcpy(arena+256,"/dirlink",9);assert(application_file_request(&m)==-21);
 m.m1_i1=CP32_APP_MKDIR;m.m1_p1=(char *)arena;m.m1_i3=263;
 assert(application_file_request(&m)==-22);
 m.m1_i3=264;m.m1_p1=(char *)arena+256;assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena+1;assert(application_file_request(&m)==-22);
 m.m1_p1=(char *)arena;memset(arena,0,sizeof(arena));arena[255]='x';assert(application_file_request(&m)==-22);
 memset(arena,0,sizeof(arena));assert(application_file_request(&m)==-2);
 ((struct cp32_app_open_request *)arena)->flags=1;assert(application_file_request(&m)==-22);
 m.m1_p1=(char *)(CP32_APP_TOP-1);
 m.m1_i1=CP32_APP_UNLINK;m.m1_i3=0;assert(application_file_request(&m)==-36);
 m.m1_i3=2;assert(application_file_request(&m)==-14);
 m.m1_p1=(char *)arena;arena[1]='x';assert(application_file_request(&m)==-22);
 m.m1_p1=(char *)(CP32_APP_TOP-1);
 m.m1_i1=CP32_APP_GETCWD;m.m1_i3=257;assert(application_file_request(&m)==-22);
 m.m1_i3=2;assert(application_file_request(&m)==-14);
 m.m1_i1=CP32_APP_WRITE;m.m1_i3=65;assert(application_file_request(&m)==-22);
 m.m1_i3=8;assert(application_file_request(&m)==-14);

 cp32_shell_app_files_reset();cp32_app_state.pid=123;return 0;
}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'disk').write_bytes(build_image(True));(p/'test.c').write_text(pre+code+request+test)
 for app in (('ls',) if '--dirs' in sys.argv else ('cat','wc','hello')):
  extra=[] if app=='cat' else [str(root/'src/apps/lib/heap.c')] if app=='hello' else [str(root/'src/apps/lib/output.c')] if app=='ls' else [str(root/'src/apps/lib/stream.c'),str(root/'src/apps/lib/output.c')]
  subprocess.run(['cc','-DCP32_ROOT_RAM=0','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',
   '-I'+str(root/'src/fs'),'-I'+str(root/'src/apps/lib'),*(['-DTEST_CAT'] if app=='cat' else ['-DTEST_HELLO'] if app=='hello' else ['-DTEST_LS'] if app=='ls' else []),
   str(p/'test.c'),str(root/f'src/apps/{app}/{app}.c'),str(root/'src/apps/lib/io.c'),*extra,
   *[str(f) for f in (root/'src/fs').glob('*.c')],'-o',str(p/app)],check=True)
  for mode in (('0','1','2','3','4','5','6','7') if app=='ls' else ('0','1','2','3','4','5','6','7','8','9',':',';','<','=','>','?','@','A','B','C','D') if app=='hello' else ('0','1','2')):subprocess.run([str(p/app),str(p/'disk'),mode],check=True)
print('App files: production FS, isolated handles, cleanup, seek, indirect reads, utilities and request bounds pass')
