"""Production multi-client FS ownership and MM-staged exec, without hardware claims."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
shell=root/'src/kernel/cp32-shell.c'
functions='\n'.join(extract_function(shell,n) for n in ('cp32_shell_clone_launch','cp32_shell_clone_process','cp32_runtime_select_fs','cp32_shell_drop_process'))
functions+='\n'+extract_function(root/'src/kernel/application.c','application_stage')
# Host backing addresses are wider than the Xtensa click field.
functions=functions.replace('(unsigned char *)(uintptr_t)(q->backing<<CLICK_SHIFT)','stage_memory')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "abi.h"
#include "image.h"
#define CP32_IRAM_EXT
#define NO_NUM 0
struct cp32_fork_state {unsigned tag;};
struct cp32_fproc {unsigned pid,input,mask;int fd;struct cp32_fproc *suspended;struct cp32_fork_state *lifecycle;};
struct proc {int p_pid;struct cp32_fproc *p_files;};
struct cp32_runtime_exec {struct proc *caller;struct cp32_app_exec_request request;unsigned backing,entry,sp,heap;char name[16];};
static struct cp32_fproc cp32_shell_state={.pid=2},cp32_root_files,*cp32_active_files=&cp32_root_files,*cp32_fproc_suspended;
static int cp32_shell_input_fd=4,clone_failure,clones,closes,closed_exec,open_error,read_error,stack_error,exec_cloexec;
static unsigned char stage_memory[32768];
static void panic(const char *s,int n){(void)s;(void)n;assert(0);}
static int cp32_fs_errno(int r){return r;}
static int cp32_fproc_clone(struct cp32_fproc *dst,const struct cp32_fproc *src,unsigned pid){
 assert(dst!=src);if(clone_failure)return -24;*dst=*src;dst->pid=pid;dst->lifecycle=0;dst->suspended=0;clones++;return 0;
}
static void cp32_fproc_exit(struct cp32_fproc *p){assert(p->pid);p->pid=0;p->fd=-1;closes++;}
static int cp32_shell_exec_open(const char *path,unsigned *size){assert(!strcmp(path,"/boot/hello"));*size=99;return open_error ? open_error:7;}
static void cp32_shell_exec_close(int fd){assert(fd==7);closed_exec++;}
static int cp32_shell_app_read(void *context,uint32_t off,unsigned char *dst,unsigned n){(void)context;(void)off;(void)dst;(void)n;return 0;}
int cp32_image_load(cp32_image_reader reader,void *context,uint32_t size,unsigned char *text,unsigned char *data,struct cp32_image *out){
 assert(reader==cp32_shell_app_read && *(int *)context==7 && size==99 && text==stage_memory && data==stage_memory+16384);
 if(read_error)return read_error;memset(text,0x4d,28672);out->entry=CP32_APP_TEXT+4;out->data.memsz=99;return 0;
}
int cp32_exec_stack(unsigned char *buffer,unsigned cap,uint32_t base,unsigned argc,const char *const argv[],unsigned envc,const char *const envp[],uint32_t *sp){
 assert(buffer==stage_memory+32768-512 && cap==512 && base==CP32_APP_TOP-512);
 assert(argc==2 && !strcmp(argv[0],"hello") && !strcmp(argv[1],"--child") && envc==3 && !strcmp(envp[0],"HOME=/"));
 for(unsigned i=28672;i<32768;i++)assert(!stage_memory[i]);
 if(stack_error)return -1;*sp=CP32_APP_TOP-64;return 0;
}
static void cp32_shell_app_files_exec(void){exec_cloexec++;}
'''
test=r'''
int main(void){
 struct cp32_fproc files[3]={0};struct cp32_fork_state states[3]={{1},{2},{3}};
 struct proc p[3]={{100,files},{101,files+1},{102,files+2}};
 assert(!cp32_shell_clone_launch(files,100) && files->input==5 && !cp32_shell_state.input);
 files->lifecycle=states;files[1].lifecycle=states+1;files[2].lifecycle=states+2;
 assert(!cp32_shell_clone_process(p,p+1) && files[1].lifecycle==states+1);
 assert(!cp32_shell_clone_process(p,p+2) && files[2].lifecycle==states+2);
 for(int n=0;n<30;n++){
  struct cp32_fproc *seen[3];int count=0;cp32_runtime_select_fs(p+n%3);
  assert(cp32_active_files==files+n%3 && !cp32_active_files->suspended);
  for(struct cp32_fproc *f=cp32_fproc_suspended;f;f=f->suspended){assert(count<2 && f!=cp32_active_files);seen[count++]=f;}
  assert(count==2 && seen[0]!=seen[1]);
 }
 cp32_shell_drop_process(p);cp32_shell_drop_process(p+2);cp32_shell_drop_process(p+1);
 assert(!cp32_fproc_suspended && cp32_active_files==&cp32_root_files && closes==3);
 clone_failure=1;files->pid=0;assert(cp32_shell_clone_launch(files,103)==-24 && !cp32_fproc_suspended && !cp32_shell_state.input);
 struct cp32_runtime_exec q={0};q.request.argc=2;strcpy(q.request.path,"/boot/hello");memcpy(q.request.args,"hello\0--child\0",14);
 open_error=-2;assert(application_stage(&q)==-2 && !closed_exec && !exec_cloexec);open_error=0;
 read_error=-1;assert(application_stage(&q)==-8 && closed_exec==1 && !exec_cloexec);read_error=-2;
 assert(application_stage(&q)==-5 && closed_exec==2 && !exec_cloexec);read_error=0;stack_error=1;
 assert(application_stage(&q)==-7 && closed_exec==3 && !exec_cloexec);stack_error=0;
 assert(!application_stage(&q) && closed_exec==4 && exec_cloexec==1 && q.sp==CP32_APP_TOP-64 && q.entry==CP32_APP_TEXT+4 && q.heap==CP32_APP_DATA+112 && !strcmp(q.name,"hello"));
 q.request.path[255]='x';assert(application_stage(&q)==-36 && exec_cloexec==1);q.request.path[255]=0;
 q.request.argc=10;assert(application_stage(&q)==-7);q.request.argc=2;memset(q.request.args,'x',sizeof(q.request.args));assert(application_stage(&q)==-7);
 puts("Runtime FS: independent registry, selection, release and transactional MM exec staging PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+functions+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(root/'src/apps/hello'),'-I'+str(root/'src/mm'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
