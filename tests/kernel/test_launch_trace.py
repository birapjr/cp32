"""Run the production launch breadcrumbs and stall timer against a USB sink."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
kernel = root / "src/kernel"
source = (kernel / "launch_trace.c").read_text()
source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "launch_trace.h"
#define HZ 60
#define MM_PROC_NR 0
#define FS_PROC_NR 1
#define LOW_USER 2
#define NR_PROCS 6
struct proc {int p_nr,p_flags;struct {unsigned pc,a[16];} p_reg;unsigned p_pid,p_app_pending;};
static struct proc table[NR_PROCS]={
    [1]={.p_nr=1,.p_flags=8}, [2]={.p_nr=-7,.p_reg={.pc=0x40381237U}},
    [3]={.p_nr=3,.p_flags=8,.p_reg={.pc=0x403d8120,.a={[0]=0x403d8100}},.p_pid=101},
    [4]={.p_nr=4,.p_reg={.pc=0x4037447f,.a={[0]=0x403726a1}},.p_pid=102,.p_app_pending=0x800},
};
#define proc_addr(n) (&table[n])
volatile struct proc *cp32_irq_saved_owner=&table[2];
static int lock_depth,in_irq;
static unsigned counter,reads;
static uint64_t systimer_unit0_read(void){assert(lock_depth);reads++;return counter;}
static char output[8192];
static size_t used;
static int lock_save(void){return lock_depth++;}
static void restore_lock(int saved){assert(lock_depth==saved+1);lock_depth=saved;}
static void usbj_print(const char *s){
    size_t n=strlen(s);
    assert((in_irq || lock_depth) && used+n<sizeof(output));
    memcpy(output+used,s,n+1);used+=n;
}
static void usbj_print_hex32(unsigned v){
    char s[12];snprintf(s,sizeof(s),"0x%08X",v);usbj_print(s);
}
static void usbj_print_u32(unsigned v){
    char s[16];snprintf(s,sizeof(s),"%u",v);usbj_print(s);
}
'''
checks = r'''
static void event(enum cp32_launch_step step,unsigned value){
    int saved=lock_depth;
    cp32_trace_launch(step,value);
    assert(lock_depth==saved);
}
static void tick(unsigned count){
    in_irq=1;
    while(count--)cp32_trace_launch_tick();
    in_irq=0;
}
static void quiet_ticks(unsigned count){
    size_t before=used;tick(count);assert(used==before);
}
int main(int argc,char **argv){
    assert(argc==2);
    event(LAUNCH_RETURN,0);quiet_ticks(1200);assert(!used);
    if(!strcmp(argv[1],"runs")){
        for(unsigned run=1;run<=4;run++){
            for(int step=LAUNCH_LOAD;step<=LAUNCH_DONE;step++){
                int saved=lock_save();
                event(step,run*16+step);
                restore_lock(saved);
                if(step!=LAUNCH_LOAD)event(step,0xbaad);
            }
            quiet_ticks(1200);
        }
    }else if(!strcmp(argv[1],"stall")){
        event(LAUNCH_LOAD,0x403d8000);quiet_ticks(599);
        event(LAUNCH_ACCEPT,0x3fceffd0);quiet_ticks(599);
        event(LAUNCH_ACCEPT,0);tick(1);quiet_ticks(1200);
        event(LAUNCH_SLOT,3);quiet_ticks(1200);
        event(LAUNCH_DONE,0);
        cp32_irq_saved_owner=NULL;
        event(LAUNCH_LOAD,0x403d8000);quiet_ticks(599);tick(1);quiet_ticks(1200);
    }else if(!strcmp(argv[1],"fork")){
        event(LAUNCH_LOAD,0x403d8000);event(LAUNCH_SLOT,3);event(LAUNCH_FORK,4);
        int saved=lock_save();
        cp32_trace_swap_end();assert(!reads);
        counter=0xfffffff0U;cp32_trace_swap_begin();counter+=0x30;
        cp32_trace_swap_end();cp32_trace_swap_begin();cp32_trace_swap_end();
        assert(reads==2);restore_lock(saved);
        event(LAUNCH_SIGNAL,12);event(LAUNCH_SIGRETURN,0x4037447f);
        quiet_ticks(599);tick(1);quiet_ticks(600);event(LAUNCH_DONE,0);
    }else{
        event(LAUNCH_LOAD,0x403d8000);
        event(LAUNCH_REPLY,(unsigned)-12);quiet_ticks(1200);
        event(LAUNCH_LOAD,0x403d8100);
        event(LAUNCH_FS_RESUME,(unsigned)-5);quiet_ticks(1200);
    }
    assert(!lock_depth);fputs(output,stdout);
}
'''

def marker(run, step, value):
    return f"[CTX V207 launch={run} {step}=0x{value:08X}]\r\n"

names = "load accept alloc slot files publish reply fs-resume rfe fork-slot swap-counts signal sigreturn exit fs-drop done".split()
expected = {
    "runs": "".join(marker(run, step, run * 16 + index)
                    for run in (1, 2) for index, step in enumerate(names)),
    "stall": (marker(1, "load", 0x403d8000) + marker(1, "accept", 0x3fceffd0)
              + "[CTX V207 stall=accept owner=0xFFFFFFF9 pc=0x40381237 mm=0x00000000 fs=0x00000008]\r\n"
              + marker(1, "slot", 3) + marker(1, "done", 0) + marker(2, "load", 0x403d8000)
              + "[CTX V207 stall=load owner=0xFFFFFFFF pc=0x00000000 mm=0x00000000 fs=0x00000008]\r\n"),
    "fork": (marker(1, "load", 0x403d8000) + marker(1, "slot", 3) + marker(1, "fork-slot", 4)
             + marker(1, "swap-counts", 0x30) + marker(1, "signal", 12) + marker(1, "sigreturn", 0x4037447f)
             + "[CTX V207 stall=sigreturn owner=0xFFFFFFF9 pc=0x40381237 mm=0x00000000 fs=0x00000008]\r\n"
             + "[CTX V207 slot=3 pid=101 flags=0x00000008 pc=0x403D8120 a0=0x403D8100 pending=0x00000000]\r\n"
             + "[CTX V207 slot=4 pid=102 flags=0x00000000 pc=0x4037447F a0=0x403726A1 pending=0x00000800]\r\n"
             + marker(1, "done", 0)),
    "errors": (marker(1, "load", 0x403d8000) + marker(1, "reply", 0xfffffff4)
               + marker(2, "load", 0x403d8100) + marker(2, "fs-resume", 0xfffffffb)),
}
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(harness + source + checks)
    exe = str(path / "trace")
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                    "-iquote", str(kernel), str(path / "test.c"), "-o", exe], check=True)
    for scenario, wanted in expected.items():
        actual = subprocess.check_output([exe, scenario])
        assert actual == wanted.encode(), (scenario, actual)
print("launch USB: two launches, unique stages, balanced locks and bounded stall snapshots pass")
