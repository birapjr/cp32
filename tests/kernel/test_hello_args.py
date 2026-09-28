from pathlib import Path
import sys,tempfile,subprocess
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
code=extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_hello')
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CP32_IRAM_EXT
struct cp32_minix_stat {unsigned size;};
static int opens,closes,runs,expected,fail;
static char printed[128];
static int cp32_shell_app_read,cp32_shell_app_output;
static int cp32_shell_open_file(const char *p){assert(!strcmp(p,"/boot/hello"));opens++;return 2;}
static int cp32_fd_fstat(int fd,struct cp32_minix_stat *s){assert(fd==2);s->size=100;return 0;}
static void cp32_fd_close(int fd){assert(fd==2);closes++;}
static void cp32_shell_print(const char *p){strcat(printed,p);}
static void cp32_shell_print_u32(unsigned n){assert(n==0);strcat(printed,"0");}
static int cp32_application_run(int read,void *ctx,uint32_t size,unsigned argc,
 const char *const argv[],int out,int *status) {
 (void)read;(void)out;assert(*(int *)ctx==2 && size==100 && argc==(unsigned)expected);
 assert(!strcmp(argv[0],"hello"));
 if(argc==3) assert(!strcmp(argv[1],"one") && !strcmp(argv[2],"two"));
 runs++;*status=0;return fail;
}
'''
tests=r'''
int main(void){
 expected=1;cp32_shell_hello("");assert(!strcmp(printed,"Hello exit=0\r\n"));
 printed[0]=0;expected=3;cp32_shell_hello("  one\t two  ");assert(runs==2);
 printed[0]=0;cp32_shell_hello("1 2 3 4 5 6 7 8 9");assert(runs==2);
 assert(!strcmp(printed,"Usage: hello [up to 8 arguments]\r\n"));
 printed[0]=0;expected=9;cp32_shell_hello("1 2 3 4 5 6 7 8");assert(runs==3);
 char longarg[65];memset(longarg,'a',64);longarg[64]=0;
 printed[0]=0;cp32_shell_hello(longarg);assert(runs==3);
 printed[0]=0;expected=1;fail=-1;cp32_shell_hello("");
 assert(!strcmp(printed,"Hello load failed\r\n") && opens==closes);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+code+tests)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
