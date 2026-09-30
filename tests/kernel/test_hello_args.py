from pathlib import Path
import sys,tempfile,subprocess
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
code='\n'.join(extract_function(root/'src/kernel/cp32-shell.c',n) for n in ('cp32_shell_app_input','cp32_shell_words','cp32_shell_launch','cp32_shell_hello'))
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define CP32_MINIX_PATH_MAX 255
#define DEV_READ 3
#define CP32_IRAM_EXT
struct cp32_minix_stat {unsigned size,mode;};
static char cp32_shell_cwd[256]="/";
static const char *expected_path="/boot/hello", *expected_name="hello";
static int denied,missing,statfail,exitcode;
static int opens,closes,runs,expected,fail;
static char printed[128];
static int flushed,input_result=5;
static void cp32_shell_flush(void) {flushed=1;}
static int cp32_shell_tty_io(int op,char *buffer,int count) {
 assert(flushed && op==DEV_READ && count==64);
 if(input_result>0) memcpy(buffer,"test\n",5);
 return input_result;
}
static int cp32_shell_app_read,cp32_shell_app_output;
static int cp32_minix_abspath(const char *cwd,const char *p,char *out){
 if(p[0]=='/') strcpy(out,p); else snprintf(out,256,"%s/%s",cwd,p);
 return 0;
}
static int cp32_shell_open_file(const char *p){assert(!strcmp(p,expected_path));if(missing)return -1;opens++;return 2;}
static int cp32_fd_fstat(int fd,struct cp32_minix_stat *s){assert(fd==2);s->size=100;s->mode=denied ? 0100444:0100555;return statfail;}
static void cp32_fd_close(int fd){assert(fd==2);closes++;}
static void cp32_shell_print(const char *p){strcat(printed,p);}
static void cp32_shell_print_u32(unsigned n){char b[16];snprintf(b,sizeof(b),"%u",n);strcat(printed,b);}
static int cp32_application_run(int read,void *ctx,uint32_t size,unsigned argc,
 const char *const argv[],int out,int *status) {
 (void)read;(void)out;assert(*(int *)ctx==2 && size==100 && argc==(unsigned)expected);
 assert(!strcmp(argv[0],expected_name));
 if(argc==3) assert(!strcmp(argv[1],"one") && !strcmp(argv[2],"two"));
 runs++;*status=exitcode;return fail;
}
'''
tests=r'''
int main(void){
 char input[64];memset(input,0x5a,sizeof(input));
 assert(cp32_shell_app_input(input,64)==5 && !memcmp(input,"test\n",5));
 assert(input[5]==0x5a);flushed=0;input_result=-1;
 assert(cp32_shell_app_input(input,64)==-1 && input[5]==0x5a);
 assert(cp32_shell_app_input(input,65)==-1);

 char words[64]; const char *av[9]; unsigned ac=99;
 strcpy(words,"\"one two\" '' a\\ b x'yz' \"c\\qd\"");
 assert(!cp32_shell_words(words,av,9,&ac) && ac==5);
 assert(!strcmp(av[0],"one two") && !strcmp(av[1],"") && !strcmp(av[2],"a b"));
 assert(!strcmp(av[3],"xyz") && !strcmp(av[4],"c\\qd"));
 strcpy(words,"'unfinished");assert(cp32_shell_words(words,av,9,&ac)==-2);
 strcpy(words,"end\\");assert(cp32_shell_words(words,av,9,&ac)==-2);
 strcpy(words,"a b");assert(cp32_shell_words(words,av,1,&ac)==-1);
 cp32_shell_hello("'unfinished");
 assert(!strcmp(printed,"Invalid command quoting\r\n") && !opens && !runs);
 printed[0]=0;
 expected=1;cp32_shell_hello("");assert(!strcmp(printed,"Hello exit=0\r\n"));
 printed[0]=0;expected=3;cp32_shell_hello("  one\t two  ");assert(runs==2);
 printed[0]=0;cp32_shell_hello("1 2 3 4 5 6 7 8 9");assert(runs==2);
 assert(!strcmp(printed,"Usage: hello [up to 8 arguments]\r\n"));
 printed[0]=0;expected=9;cp32_shell_hello("1 2 3 4 5 6 7 8");assert(runs==3);
 char longarg[65];memset(longarg,'a',64);longarg[64]=0;
 printed[0]=0;cp32_shell_hello(longarg);assert(runs==3);
 printed[0]=0;expected=1;fail=-1;cp32_shell_hello("");
 assert(!strcmp(printed,"Hello load failed\r\n") && opens==closes);
 fail=0;expected_path="/boot/echo";expected_name="/boot/echo";expected=3;
 printed[0]=0;cp32_shell_launch("/boot/echo one two",0);
 assert(!strcmp(printed,"Application exit=0\r\n") && opens==closes);
 strcpy(cp32_shell_cwd,"/boot");expected_name="echo";
 printed[0]=0;cp32_shell_launch("echo one two",0);
 assert(!strcmp(printed,"Application exit=0\r\n"));
 for(int repeat=0;repeat<12;repeat++) {
   int before=runs;
   printed[0]=0;denied=1;cp32_shell_launch("echo",0);
   assert(!strcmp(printed,"File is not executable\r\n") && runs==before);
   denied=0;missing=1;printed[0]=0;cp32_shell_launch("echo",0);
   assert(!strcmp(printed,"Cannot open executable\r\n") && runs==before);
   missing=0;statfail=-1;printed[0]=0;cp32_shell_launch("echo",0);
   assert(!strcmp(printed,"Application load failed\r\n") && runs==before);
   statfail=0;fail=-1;expected=1;printed[0]=0;cp32_shell_launch("echo",0);
   assert(!strcmp(printed,"Application load failed\r\n"));fail=0;
   printed[0]=0;cp32_shell_launch("echo",0);
   assert(!strcmp(printed,"Application exit=0\r\n") && opens==closes);
 }
 printed[0]=0;cp32_shell_launch("",0);assert(strstr(printed,"Usage: run"));
 printed[0]=0;cp32_shell_launch("echo 1 2 3 4 5 6 7 8 9",0);assert(strstr(printed,"Usage: run"));
 /* Bare commands use /boot independently of cwd; slash bypasses lookup. */
 strcpy(cp32_shell_cwd,"/elsewhere");expected_path="/boot/echo";
 expected_name="echo";expected=3;printed[0]=0;
 cp32_shell_launch("echo one two",2);
 assert(!strcmp(printed,"Application exit=0\r\n") && opens==closes);
 expected_path="/elsewhere/./echo";expected_name="./echo";expected=1;
 printed[0]=0;cp32_shell_launch("./echo",2);
 assert(!strcmp(printed,"Application exit=0\r\n"));
 expected_path="/boot/echo";expected_name="/boot/echo";
 printed[0]=0;cp32_shell_launch("/boot/echo",2);
 assert(!strcmp(printed,"Application exit=0\r\n"));
 missing=1;printed[0]=0;cp32_shell_launch("/boot/echo",2);
 assert(!strcmp(printed,"Command not found\r\n"));missing=0;
 denied=1;printed[0]=0;cp32_shell_launch("/boot/echo",2);
 assert(!strcmp(printed,"File is not executable\r\n"));denied=0;
 strcpy(cp32_shell_cwd,"/boot");expected_name="echo";
 printed[0]=0;exitcode=-7;cp32_shell_launch("echo",0);assert(!strcmp(printed,"Application exit=-7\r\n"));

}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+code+tests)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
