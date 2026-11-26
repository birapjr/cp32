"""Production image pin and FS dispatch: selection, validation and balanced release."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
source='\n'.join(extract_function(root/'src/kernel/application.c',n) for n in
 ('cp32_application_window_busy','cp32_application_window_owner','application_pin','application_unpin','application_file_request'))
pre=r'''
#include <assert.h>
#include <stdint.h>
#define CP32_IRAM_EXT
#define CP32_APP_DATA 0x3fcec000U
#define CP32_APP_TOP 0x3fcf0000U
#define NIL_PROC ((struct proc *)0)
typedef struct {int m1_i1,m1_i2,m1_i3;char *m1_p1;} message;
struct proc {int pid;};
static struct proc processes[2],*application_io_owner,*resident,*selected;
static volatile unsigned application_io_pins;
static int locked,activation,validation,result,called,activations;
static int lock_save(void){assert(!locked);locked=1;return 0;}
static void restore_lock(int n){assert(locked && !n);locked=0;}
static int cp32_runtime_activate(struct proc *p){assert(locked && !application_io_pins);activations++;if(!activation)resident=p;return activation;}
static void cp32_runtime_select_fs(struct proc *p){assert(!locked && application_io_pins && application_io_owner==p && resident==p);selected=p;}
static int cp32_fproc_validate(int op,char *p,unsigned n,uintptr_t lo,uintptr_t hi) {
 (void)op;(void)p;(void)n;assert(lo==CP32_APP_DATA && hi==CP32_APP_TOP);
 assert(application_io_pins && selected==resident && application_io_owner==resident);return validation;
}
static int cp32_shell_app_file(int op,int fd,char *p,unsigned n) {
 (void)op;(void)fd;(void)p;(void)n;assert(application_io_pins);called++;return result;
}
'''
test=r'''
int main(void){message m={0};assert(!cp32_application_window_busy());
 activation=-16;assert(application_pin(processes)==-16 && !application_io_pins && !selected);
 activation=0;assert(!application_pin(processes) && application_io_pins && cp32_application_window_owner()==processes);
 assert(application_pin(processes+1)==-16 && activations==2 && resident==processes);
 validation=-14;assert(application_file_request(&m)==-14 && !called && application_io_pins);
 validation=0;result=-5;assert(application_file_request(&m)==-5 && called==1 && application_io_pins);
 application_unpin();assert(!application_io_pins && !cp32_application_window_owner());
 assert(!application_pin(processes+1) && resident==processes+1 && selected==processes+1);
 result=64;assert(application_file_request(&m)==64 && called==2 && application_io_pins);
 application_unpin();assert(!application_io_pins && !locked);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+source+test)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('I/O pin: atomic resident selection, competing caller rejection, errors, release PASS')
