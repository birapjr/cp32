from pathlib import Path
import sys,tempfile,subprocess
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
pre=r'''
#include <assert.h>
#include <string.h>
#define CP32_IRAM_EXT
#define DEV_WRITE 4
#define NO_NUM 0
static char usb[1024],lcd[1024],cp32_shell_output[256];
static unsigned cp32_shell_output_len;static int cp32_shell_usb_only,writes;
static void lock(void){} static void unlock(void){}
static void usbj_print(const char *s){strcat(usb,s);}
static void panic(const char *s,int n){(void)s;(void)n;assert(0);}
static int cp32_shell_tty_io(int op,char *p,int n){assert(op==4);strncat(lcd,p,n);writes++;return n;}
'''
code='\n'.join(extract_function(root/'src/kernel/cp32-shell.c',n) for n in ['cp32_shell_flush','cp32_shell_display','cp32_shell_print','cp32_shell_app_output','cp32_shell_test_progress'])
test=r'''
int main(void){
 cp32_shell_print("before\n");cp32_shell_flush();assert(writes==1);
 cp32_shell_usb_only=1;
 cp32_shell_print("[TEST case]\n");cp32_shell_app_output("app\n",4);cp32_shell_print("[TEST ALL PASS]\r\n");cp32_shell_print("Hello exit=0\r\n");
 assert(!cp32_shell_output_len && writes==1 && !strcmp(lcd,"before\n"));
 assert(strstr(usb,"app\r\n") && strstr(usb,"Hello exit=0"));
 cp32_shell_test_progress("\r\n",2);
 for(int i=0;i<3;i++)cp32_shell_test_progress(".",1);
 assert(cp32_shell_usb_only && writes==5 && !strstr(usb,"..."));
 cp32_shell_usb_only=0;cp32_shell_display("\r\n");cp32_shell_display("[TEST ALL PASS]\r\n");cp32_shell_flush();
 assert(!strcmp(lcd,"before\n\r\n...\r\n[TEST ALL PASS]\r\n") && writes==6);
 cp32_shell_print("normal");cp32_shell_flush();assert(strstr(lcd,"normal") && strstr(usb,"normal"));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+code+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('suite output: production routing USB-only, final LCD marker and normal output restoration PASS')
