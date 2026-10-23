from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
code=extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_df')
pre=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "fs.h"
static char output[256];static int fail;
static void cp32_shell_print(const char *s){strcat(output,s);}
static void cp32_shell_print_u32(uint32_t n){char s[20];snprintf(s,sizeof(s),"%u",n);strcat(output,s);}
static unsigned cp32_root_capacity(void){return 65536;}
static int cp32_shell_disk_read(unsigned at,char *p,int n){(void)at;(void)p;(void)n;return -1;}
int cp32_minix_space_read(cp32_disk_reader read,unsigned capacity,struct cp32_minix_space *info){
 assert(read==cp32_shell_disk_read && capacity==65536);
 if(fail)return -CP32_SUPER_IO;
 *info=(struct cp32_minix_space){2048,26,25,17,16,14};return 0;
}
'''
post=r'''
int main(void){cp32_shell_df();assert(!strcmp(output,"Free KiB 50 / 52\r\nFree inodes 16 / 17\r\n"));
 output[0]=0;fail=1;cp32_shell_df();assert(!strcmp(output,"Space read failed: 4\r\n"));}
'''
with tempfile.TemporaryDirectory() as folder:
 p=Path(folder);(p/'test.c').write_text(pre+code+post)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I'+str(root/'src/fs'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('df: production shell formatting, scaled KiB and I/O error output pass')
