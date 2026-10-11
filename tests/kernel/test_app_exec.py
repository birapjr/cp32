"""Exercise production replacement with real FS/ELF/stack code; model IPC/hardware."""
from pathlib import Path
import subprocess, sys, tempfile, struct
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
sys.path.insert(0,str(root/'tools'))
from make_minix_demo import build_image
shell=root/'src/kernel/cp32-shell.c'
code='\n'.join(extract_function(shell,n) for n in ('cp32_shell_open_file','cp32_shell_app_files_reset','cp32_app_file_error','cp32_shell_exec_open','cp32_shell_exec_close','cp32_shell_app_file','cp32_shell_app_read'))
replace=extract_function(root/'src/kernel/application.c','application_replace')
# Only physical-address accesses and instruction synchronization are modeled.
replace=replace.replace('memcpy(&copy,request->m1_p1,sizeof(copy));','memcpy(&copy,live_data+(p-CP32_APP_DATA),sizeof(copy));')
replace=replace.replace('stage=(unsigned char *)(uintptr_t)(base<<CLICK_SHIFT);','stage=staging;')
replace=replace.replace('(void *)APP_TEXT_DATA','live_text').replace('(void *)CP32_APP_DATA','live_data')
replace=replace.replace('__asm__ volatile("memw\\n\\tisync" ::: "memory");','syncs++;')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fs.h"
#include "io.h"
#include "image.h"
#define OK 0
#define EINVAL -22
#define NO_NUM 0
#define ANY -1
#define NIL_PROC ((struct proc *)0)
#define RECEIVING 8
#define FS_PROC_NR 1
#define MM_PROC_NR 0
#define CP32_MM_ALLOCATE 3001
#define CP32_MM_RELEASE 3002
#define CLICK_SHIFT 12
typedef uintptr_t reg_t;
typedef struct {int m_type,m_source,m1_i1,m1_i3;char *m1_p1;} message;
struct proc {
 struct {reg_t a[16],pc,psw,sp,sar,lbeg,lend,lcount;} p_reg;
 int p_pid,p_flags,p_getfrom,p_sendto;
 struct proc *p_callerq,*p_sendlink;
 int p_blocked_frame_valid,p_blocked_frame_result;
 reg_t p_blocked_frame_pc,p_blocked_frame_psw,p_blocked_frame_sp;
 message *p_messbuf;
 char p_name[16];
};
static unsigned char disk[65536],live_text[16384],live_data[16384],staging[32768];
static unsigned char old_text[16384],old_data[16384];
static int cp32_app_fds[CP32_APP_FILE_MAX];
static char cp32_shell_cwd[256]="/";
static unsigned cp32_root_capacity(void) {return sizeof(disk);}
static int read_count,fail_read;
static int cp32_shell_disk_read(unsigned off,char *out,int count) {
 if(++read_count==fail_read)return -1;
 if(off>sizeof(disk) || (unsigned)count>sizeof(disk)-off)return -1;
 memcpy(out,disk+off,count);return count;
}
static struct proc child;
static uint32_t floor_value,break_value;
static int services,syncs,publishes,allocated,alloc_fail,allocs,releases;
static void panic(const char *s,int n) {(void)n;fprintf(stderr,"panic %s\n",s);assert(0);}
static int lock_save(void) {return 7;}
static void restore_lock(int n) {assert(n==7);}
static void lock_ready(struct proc *p) {
 assert(p==&child && !p->p_flags && p->p_pid==123);
 assert(!p->p_blocked_frame_valid && !p->p_messbuf && p->p_getfrom==ANY);
 assert(p->p_reg.pc==CP32_APP_TEXT && !(p->p_reg.sp&15));
 assert(p->p_reg.a[1]==p->p_reg.sp && p->p_reg.a[15]==p->p_reg.sp);
 assert(p->p_reg.a[2]==(reg_t)&services && p->p_reg.psw==0x100);
 assert(!strcmp(p->p_name,"hello"));publishes++;
}
static int _sendrec(int nr,message *m) {
 assert(nr==MM_PROC_NR);m->m_source=nr;
 if(m->m_type==CP32_MM_ALLOCATE) {
  assert(!allocated && m->m1_i1==8);allocs++;
  m->m_type=alloc_fail ? -12:0;m->m1_i1=alloc_fail ? 0:0x1000;allocated=!alloc_fail;
 } else {assert(m->m_type==CP32_MM_RELEASE && allocated && m->m1_i1==0x1000);allocated=0;releases++;m->m_type=0;}
 return 0;
}
'''
test=r'''
static void setup(void) {
 memset(&child,0,sizeof(child));child.p_pid=123;child.p_flags=RECEIVING;child.p_getfrom=FS_PROC_NR;
 child.p_blocked_frame_valid=1;child.p_blocked_frame_pc=99;child.p_messbuf=(message *)1234;
 memset(live_text,0xa5,sizeof(live_text));memset(live_data,0x5a,sizeof(live_data));
 floor_value=CP32_APP_DATA+64;break_value=floor_value+128;syncs=publishes=read_count=0;
}
static int attempt(struct cp32_app_exec_request *q,int expected) {
 memcpy(live_data,q,sizeof(*q));memcpy(old_data,live_data,sizeof(old_data));memcpy(old_text,live_text,sizeof(old_text));
 struct proc old=child;uint32_t f=floor_value,b=break_value;
 message m={0,0,0,sizeof(*q),(char *)CP32_APP_DATA};
 int r=application_replace(&child,&m,&floor_value,&break_value);
 if(expected!=999)assert(r==expected);
 assert(!allocated);
 if(r) {
  assert(!memcmp(&child,&old,sizeof(child)) && !memcmp(live_text,old_text,sizeof(live_text)) && !memcmp(live_data,old_data,sizeof(live_data)));
  assert(f==floor_value && b==break_value && !syncs && !publishes);
 } else {
  assert(syncs==1 && publishes==1 && floor_value==break_value);
  assert(live_text[0]==0x11 && live_data[0]==0x22);
  for(unsigned i=4;i<12288;i++)assert(!live_data[i]);
  uint32_t *sp=(uint32_t *)(live_data+(child.p_reg.sp-CP32_APP_DATA));
  assert(sp[0]==2 && !strcmp((char *)live_data+(sp[1]-CP32_APP_DATA),"alias"));
  assert(!strcmp((char *)live_data+(sp[2]-CP32_APP_DATA),"argument"));
  assert(!sp[3] && !sp[7]);
 }
 return r;
}
static int bridge(unsigned op,int fd,void *buffer,unsigned n) {
 if(op!=CP32_APP_EXEC)return cp32_shell_app_file(op,fd,buffer,n);
 assert(n==sizeof(struct cp32_app_exec_request));
 return attempt(buffer,-2);
}
static int put(const char *p,unsigned n) {(void)p;return n;}
static int get(char *p,unsigned n) {(void)p;(void)n;return 0;}
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(disk,1,sizeof(disk),f)==sizeof(disk));fclose(f);
 struct cp32_app_exec_request q={"/boot/hello","alias\0argument",2};
 setup();
 /* Full application handle table must still permit the loader's private fd. */
 for(int i=0;i<4;i++)assert(cp32_shell_app_file(CP32_APP_OPEN,0,"/readme",8)==i+3);
 char buf[8];assert(cp32_shell_app_file(CP32_APP_READ,3,buf,5)==5);
 strcpy(q.path,"/missing");attempt(&q,-2);
 strcpy(q.path,"/readme/no");attempt(&q,-20);
 strcpy(q.path,"/readme");attempt(&q,-13);
 strcpy(q.path,"/boot");attempt(&q,-13);
 strcpy(q.path,"/boot/echo");attempt(&q,-8); /* executable, invalid ELF */
 strcpy(q.path,"/boot/hello");alloc_fail=1;attempt(&q,-12);alloc_fail=0;
 q.argc=10;attempt(&q,-7);q.argc=2;
 memset(q.args,'x',sizeof(q.args));attempt(&q,-7);memset(q.args,0,sizeof(q.args));memcpy(q.args,"alias\0argument",15);
 q.path[255]='x';attempt(&q,-36);q.path[255]=0;
 message m={0,0,0,sizeof(q),(char *)(CP32_APP_TOP-1)};
 assert(application_replace(&child,&m,&floor_value,&break_value)==-14);
 m.m1_i3--;assert(application_replace(&child,&m,&floor_value,&break_value)==-22);
 /* Inject every disk read failure including reads after partial payload load. */
 setup();attempt(&q,0);int reads=read_count,failed=0;
 for(int i=1;i<=reads;i++) {
  setup();fail_read=i;
  int r=attempt(&q,999);fail_read=0;
  if(r)failed++;
  assert(cp32_shell_app_file(CP32_APP_SEEK_CUR,3,0,0)==5);
 }
 assert(failed>5);
 for(int i=0;i<40;i++) {setup();attempt(&q,0);assert(cp32_shell_app_file(CP32_APP_SEEK_CUR,3,0,0)==5);}
 assert(allocs==releases+1);
 setup();struct cp32_app_services api={8,put,0,0,0,0,get,bridge};assert(!cp32_io_init(&api));
 const char *args[]={"alias","argument",0};
 assert(cp32_execv("/missing",args)==-1 && cp32_errno==CP32_ENOENT);
 assert(cp32_execv(0,args)==-1 && cp32_errno==CP32_EFAULT);
 const char *empty[]={0};assert(cp32_execv("/boot/hello",empty)==-1 && cp32_errno==CP32_E2BIG);
 const char *many[]={"a","b","c","d","e","f","g","h","i","j",0};
 assert(cp32_execv("/boot/hello",many)==-1 && cp32_errno==CP32_E2BIG);
 char big[257];memset(big,'a',256);big[256]=0;const char *large[]={big,0};
 assert(cp32_execv("/boot/hello",large)==-1 && cp32_errno==CP32_E2BIG);
 assert(cp32_execv(big,args)==-1 && cp32_errno==CP32_ENAMETOOLONG);
 cp32_shell_app_files_reset();puts("exec: rollback on disk/MM/image/request failures; PID/frame/argv/BSS and full fd-table preservation pass");
}
'''
# Valid two-segment fixture with multiple payload reads and BSS.
elf=bytearray(384)
elf[:16]=b'\x7fELF\x01\x01\x01'+bytes(9)
struct.pack_into('<HHIIIIIHHHHHH',elf,16,2,94,1,0x403d8000,52,0,0,52,32,2,0,0,0)
struct.pack_into('<8I',elf,52,1,128,0x403d8000,0x403d8000,252,256,5,4)
struct.pack_into('<8I',elf,84,1,380,0x3fcec000,0x3fcec000,4,32,6,4)
elf[128:380]=bytes([0x11])*252
elf[380:]=bytes([0x22])*4
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'disk').write_bytes(build_image(True,hello=elf,echo=b'bad ELF'))
 (p/'test.c').write_text(pre+code+extract_function(root/'src/kernel/context.c','cp32_exec_frame')+replace+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root/'src/fs'),'-I'+str(root/'src/mm'),'-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'src/mm/image.c'),str(root/'src/mm/exec.c'),str(root/'src/apps/lib/io.c'),*[str(f) for f in (root/'src/fs').glob('*.c')],'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'disk')],check=True)
