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
code='\n'.join(extract_function(shell,n) for n in ('cp32_shell_clock','cp32_shell_app_files_reset','cp32_shell_app_file','cp32_shell_date','cp32_shell_touch'))
code+='\nstatic int time_now(unsigned *now) {return cp32_shell_clock(0,now); }\n'
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
typedef struct {int m_type,m_source;unsigned NEW_TIME;int m1_i1,m1_i2,m1_i3;char *m1_p1;} message;
#include "fproc.h"
static char cp32_shell_previous[256];
static struct cp32_fproc cp32_shell_state={.pid=1,.root_inode=1,.cwd="/"};
static struct cp32_fproc cp32_app_state={.pid=123,.root_inode=1,.mask=0022,.cwd="/"};

#define cp32_shell_cwd cp32_app_state.cwd
#define CP32_DEV_SYNC 11
#ifndef OK
#define OK 0
#endif
#define CLOCK -3
#define GET_TIME 3
#define SET_TIME 4
static unsigned clock_seconds;
static int clock_failure,clock_corrupt;
static int clock_sendrec(int endpoint,message *m) {
 assert(endpoint==CLOCK);
 if(clock_failure)return -5;
 if(m->m_type==SET_TIME)clock_seconds=m->NEW_TIME;
 else {assert(m->m_type==GET_TIME);m->NEW_TIME=clock_seconds;}
 m->m_source=clock_corrupt ? 99:CLOCK;m->m_type=OK;return OK;
}
#define _sendrec clock_sendrec
static int sync_result,sync_calls;
static int cp32_shell_disk_io(int op,unsigned offset,char *buffer,int count) {
 assert(op==CP32_DEV_SYNC && !offset && !buffer && !count);sync_calls++;return sync_result;
}
static unsigned cp32_root_capacity(void) {return sizeof(disk);}
static int write_failure;
static int cp32_shell_disk_write(unsigned off,const char *p,int n) {
 if(write_failure)return n-1;
 if(off>sizeof(disk) || (unsigned)n>sizeof(disk)-off)return -1;
 memcpy(disk+off,p,n);return n;
}
static int cp32_shell_disk_read(unsigned off,char *out,int count) {
 if(off>sizeof(disk) || (unsigned)count>sizeof(disk)-off)return -1;
 memcpy(out,disk+off,count);return count;
}
static char output[4096];static unsigned used;static int fail_output;
static int put(const char *p,unsigned n) {if(fail_output)return -1;assert(used+n<sizeof(output));memcpy(output+used,p,n);used+=n;return n;}
static void cp32_shell_print(const char *s) {put(s,strlen(s));}
static void cp32_shell_print_u32(unsigned v) {char b[16];snprintf(b,sizeof(b),"%u",v);cp32_shell_print(b);}
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
 (void)time_now;(void)cp32_shell_date;(void)cp32_shell_touch;assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);
 assert(fread(disk,1,sizeof(disk),f)==sizeof(disk));fclose(f);
 int mode=argv[2][0]-'0';
 struct cp32_app_services api={7,put,quit,0,0,0,input,cp32_shell_app_file};
#ifdef TEST_HELLO
 if(mode==25) {
   api.version=25;assert(!cp32_io_init(&api));
   cp32_fs_time_bind(cp32_shell_disk_read,cp32_shell_disk_write,time_now);
   clock_seconds=1000;int fd=cp32_open_flags("/auto",CP32_O_CREAT|CP32_O_RDWR,0600);
   assert(fd>=3);struct cp32_app_stat st;char buf[8];
   assert(!cp32_fstat(fd,&st) && st.atime==1000 && st.mtime==1000 && st.ctime==1000);
   clock_seconds=2000;assert(cp32_write(fd,"hi",2)==2);
   assert(!cp32_fstat(fd,&st) && st.atime==1000 && st.mtime==2000 && st.ctime==2000);
   clock_seconds=3000;assert(cp32_pread(fd,buf,2,0)==2 && !memcmp(buf,"hi",2));
   assert(!cp32_fstat(fd,&st) && st.atime==3000 && st.mtime==2000 && st.ctime==2000);
   clock_seconds=4000;assert(cp32_pread(fd,buf,1,2)==0 && cp32_write(fd,"",0)==0);
   assert(!cp32_fstat(fd,&st) && st.atime==4000 && st.mtime==2000);
   assert(!cp32_link("/auto","/other"));
   assert(cp32_pwrite(fd,"yo",2,0)==2);
   assert(!cp32_stat("/other",&st) && st.mtime==4000 && st.ctime==4000);
   clock_seconds=5000;assert(!cp32_ftruncate(fd,1));
   assert(!cp32_fstat(fd,&st) && st.size==1 && st.mtime==5000 && st.atime==4000);
   clock_seconds=6000;assert(!cp32_ftruncate(fd,4));
   assert(!cp32_fstat(fd,&st) && st.size==4 && st.mtime==6000);
   clock_seconds=7000;assert(!cp32_unlink("/auto") && !cp32_unlink("/other"));
   assert(cp32_pwrite(fd,"x",1,0)==1 && !cp32_fstat(fd,&st) && st.links==0 && st.mtime==7000);
   clock_seconds=8000;assert(cp32_pread(fd,buf,4,0)==4 && buf[0]=='x' && !buf[1] && !buf[3]);
   assert(!cp32_fstat(fd,&st) && st.atime==8000);
   clock_seconds=9000;clock_failure=1;buf[0]='q';
   assert(cp32_pread(fd,buf,1,0)==-1 && buf[0]=='q');
   assert(cp32_pwrite(fd,"z",1,0)==-1);
   clock_failure=0;assert(!cp32_fstat(fd,&st) && st.mtime==7000 && st.atime==8000);
   write_failure=1;buf[0]='q';assert(cp32_pread(fd,buf,1,0)==-1 && buf[0]=='q');
   write_failure=0;assert(cp32_pread(fd,buf,1,0)==1 && buf[0]=='x');
   assert(!cp32_close(fd));
   clock_seconds=10000;assert(cp32_minix_create(cp32_shell_disk_read,cp32_shell_disk_write,sizeof(disk),"/shell","hi",2)==2);
   clock_seconds=11000;assert(cp32_minix_patch(cp32_shell_disk_read,cp32_shell_disk_write,sizeof(disk),"/shell",0,"yo",2)==2);
   assert(!cp32_stat("/shell",&st) && st.atime==10000 && st.mtime==11000);
   clock_seconds=12000;assert(cp32_minix_append(cp32_shell_disk_read,cp32_shell_disk_write,sizeof(disk),"/shell","!",1)==1);
   assert(!cp32_stat("/shell",&st) && st.mtime==12000 && st.size==3);
   cp32_fs_time_bind(cp32_shell_disk_read,0,time_now);clock_failure=1;
   fd=cp32_open("/shell");assert(fd>=3 && cp32_read(fd,buf,3)==3 && !memcmp(buf,"yo!",3));
   assert(!cp32_fstat(fd,&st) && st.atime==10000);assert(!cp32_close(fd));
   return 0;
 }
 if(mode==24) {
   api.version=25;assert(!cp32_io_init(&api));unsigned seconds=99;
   cp32_shell_date("2147483648");assert(strstr(output,"Usage:"));
   cp32_shell_date("1000");assert(clock_seconds==1000);
   cp32_shell_touch("/touched");struct cp32_app_stat touched;
   assert(!cp32_stat("/touched",&touched) && touched.size==0 && touched.mtime==1000);
   clock_seconds=0;
   assert(!cp32_time(&seconds) && seconds==0);
   assert(!cp32_stime(1000) && !cp32_time(&seconds) && seconds==1000);
   int fd=cp32_creat("/times",0644);assert(fd>=3);
   assert(cp32_write(fd,"hi",2)==2 && !cp32_close(fd));
   struct cp32_app_stat st;struct cp32_utimbuf times={100,200};
   assert(!cp32_utime("/times",&times) && !cp32_stat("/times",&st));
   assert(st.atime==100 && st.mtime==200 && st.ctime==1000 && st.size==2);
   cp32_shell_touch("/times");assert(!cp32_stat("/times",&st) && st.size==2);
   assert(cp32_fproc_utime(&cp32_app_state,cp32_shell_disk_read,0,sizeof(disk),"/times",0,0,1,1)==-30);
   assert(cp32_fproc_validate(CP32_APP_TIME,(char *)arena,3,(uintptr_t)arena,(uintptr_t)arena+512)==-14);
   assert(cp32_fproc_validate(CP32_APP_UTIME,(char *)arena,268,(uintptr_t)arena+1,(uintptr_t)arena+512)==-14);
   assert(!cp32_link("/times","/alias") && !cp32_stime(2000));
   assert(!cp32_utime("/alias",0) && !cp32_stat("/times",&st));
   assert(st.atime==2000 && st.mtime==2000 && st.ctime==2000);
   cp32_app_state.euid=1;
   assert(cp32_stime(3000)==-1 && cp32_errno==CP32_EPERM);
   assert(cp32_utime("/times",&times)==-1 && cp32_errno==CP32_EPERM);
   assert(cp32_utime("/times",0)==-1 && cp32_errno==CP32_EACCES);
   cp32_app_state.euid=0;
   assert(!cp32_chmod("/times",0666));cp32_app_state.euid=1;
   assert(!cp32_utime("/times",0));cp32_app_state.euid=0;
   assert(!cp32_utime("/boot",0));
   assert(cp32_utime("/absent",0)==-1 && cp32_errno==CP32_ENOENT);
   assert(cp32_stime(0x80000000U)==-1 && cp32_errno==CP32_EINVAL);
   clock_failure=1;seconds=55;
   assert(cp32_time(&seconds)==-1 && cp32_errno==CP32_EIO && seconds==55);
   clock_failure=0;clock_corrupt=1;
   assert(cp32_time(&seconds)==-1 && seconds==55);clock_corrupt=0;
   assert(!cp32_stat("/times",&st));unsigned oldtime=st.mtime;
   write_failure=1;assert(cp32_utime("/times",&times)==-1 && cp32_errno==CP32_EIO);
   write_failure=0;assert(!cp32_stat("/times",&st) && st.mtime==oldtime);
   clock_seconds=0x80000000U;
   assert(cp32_time(&seconds)==-1 && seconds==55);
   api.version=24;assert(cp32_time(&seconds)==-1 && cp32_errno==CP32_ENOSYS);
   return 0;
 }
 if(mode==23) {
   assert(cp32_fproc_validate(CP32_APP_SYNC,(char *)1,0,0,512)==-22);
   assert(cp32_fproc_validate(CP32_APP_FSYNC,0,1,0,512)==-22);
   api.version=24;char *args[]={"hello","--sync",0};
   assert(!app_main(2,args,0,&api) && sync_calls==2);
   int fd=cp32_creat("/synctest",0600);assert(fd>=3);
   assert(cp32_write(fd,"ok",2)==2);int copy=cp32_dup(fd);assert(copy>=3);
   assert(!cp32_fsync(copy) && cp32_lseek(fd,0,1)==2);
   assert(!cp32_unlink("/synctest") && !cp32_fsync(fd));
   assert(!cp32_close(copy) && !cp32_close(fd));
   fd=cp32_opendir("/");assert(fd>=3 && !cp32_fsync(fd) && !cp32_close(fd));
   sync_result=-5;
   assert(cp32_sync()==-1 && cp32_errno==CP32_EIO);
   assert(cp32_fsync(0)==-1 && cp32_errno==CP32_EBADF);
   api.version=23;
   assert(cp32_sync()==-1 && cp32_errno==CP32_ENOSYS);
   return 0;
 }
 if(mode==22) {
   api.version=23;char *args[]={"hello","--rename",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"rename stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Rename OK"));
   assert(cp32_rename("","/no")==-1 && cp32_errno==CP32_ENOENT);
   api.version=22;assert(cp32_rename("/no","/none")==-1 && cp32_errno==CP32_ENOSYS);
   cp32_shell_app_files_reset();return 0;
 }
 if(mode==21) {
   api.version=22;char *args[]={"hello","--grow",0};
   int r=app_main(2,args,0,&api);if(r)fprintf(stderr,"growth stage %d errno %d\n",r,cp32_errno);
   assert(!r && strstr(output,"Growth OK"));cp32_shell_app_files_reset();return 0;
 }
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
 message m={.m1_i1=CP32_APP_OPEN,.m1_i3=7,.m1_p1=(char *)arena};memcpy(arena,"readme",7);
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
 m.m1_i1=CP32_APP_RENAME;m.m1_p1=(char *)arena;m.m1_i3=511;assert(application_file_request(&m)==-22);
 m.m1_i3=512;memset(arena,0,sizeof(arena));arena[511]='x';assert(application_file_request(&m)==-36);
 m.m1_p1=(char *)arena+1;assert(application_file_request(&m)==-14);
 memset(arena,0,sizeof(arena));
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
  for mode in (('0','1','2','3','4','5','6','7') if app=='ls' else ('0','1','2','3','4','5','6','7','8','9',':',';','<','=','>','?','@','A','B','C','D','E','F','G','H','I') if app=='hello' else ('0','1','2')):subprocess.run([str(p/app),str(p/'disk'),mode],check=True)
  if app=='hello' and '--dirs' not in sys.argv:
   sys.path.insert(0,str(root/'tests/fs'))
   from test_fsck import swapped
   (p/'swapped').write_bytes(swapped(build_image(True)))
   for time_mode in ('H','I'):subprocess.run([str(p/app),str(p/'swapped'),time_mode],check=True)
print('App files: production FS, isolated handles, cleanup, seek, indirect reads, utilities and request bounds pass')
